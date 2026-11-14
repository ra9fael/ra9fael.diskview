// In-process, read-only MFT scan using the token supplied by the host.
//
// The QML window wants the flat arrays of ScanTree.h, not the nested Node tree
// the native edition draws. Building Node first and flattening it means two
// complete trees and a hash index per record live at once; on a full C: volume
// under the host's 2 GB job cap that peak turned a finished read into
// bad_alloc, and the old fallback then ran a second (ordinary) scan on top.
//
// This builder never creates that intermediate tree. It keeps one entry per
// name (the read used to keep two: a name-less base carrier and the name), a
// record-indexed parent table and a CSR child list, then writes the model's
// parallel arrays in parent-before-child order.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include "ScanTree.h"
#include "mft_scan.h"

namespace diskview::fastscan {
enum class Outcome {
    Complete,
    Cancelled,
    // The reader could not run at all (not a fixed NTFS root, volume refused,
    // damaged records): ordinary enumeration is a sensible substitute.
    Fallback,
    // The read succeeded but assembling the result exceeded memory. Running
    // ordinary enumeration on top of that would repeat the peak, so the caller
    // reports it instead of silently rescanning.
    MemoryFailure,
};

inline bool processElevated() {
    HANDLE token=nullptr;
    if(!::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    struct Guard {HANDLE token;~Guard(){if(token)::CloseHandle(token);}} guard{token};
    TOKEN_ELEVATION elevation{};
    DWORD size=0;
    return ::GetTokenInformation(token,TokenElevation,&elevation,sizeof elevation,&size)&&elevation.TokenIsElevated;
}

// Base-record sizes: an $FILE_NAME may live in an extension record that does
// not repeat the unnamed $DATA size, so the reader's name-less carrier entry
// still carries it. Keep just (record, bytes) instead of a whole Entry.
inline void appendBatch(mft::Batch& batch, std::deque<mft::Entry>& entries,
                        std::vector<std::pair<std::uint32_t, std::uint64_t>>& baseSizes,
                        std::uint64_t& files, std::uint64_t& bytes) {
    for (auto& entry : batch.entries) {
        if (entry.name.empty()) {
            if (entry.known) baseSizes.emplace_back(ws::ntfs::recordOf(entry.id), entry.bytes);
            continue; // the redundant per-record carrier
        }
        if (!entry.directory) {
            ++files;
            if (entry.known) bytes += entry.bytes;
        }
        entries.push_back(std::move(entry));
    }
    batch.entries.clear();
}

namespace detail {
struct Frame {
    int node = 0;       // entry index, or rootNode for the pseudo root
    int newIndex = -1;  // index in the FlatTree being written
    std::wstring path;
    int childPos = 0;
};

inline bool excludedPath(const std::wstring& path, const ScanOptions& options) {
    for (const auto& base : options.excludedPaths) {
        if (!base.empty() && diskview::sameOrBelow(path, base)) return true;
    }
    return false;
}
} // namespace detail

// Builds the model's parallel arrays from the read batches. Kept separate
// from the reader so ordering, exclusion and totals can be unit tested without
// a volume.
inline Outcome buildFlat(std::deque<mft::Entry>& entries,
                         std::vector<std::pair<std::uint32_t, std::uint64_t>> baseSizes,
                         std::uint32_t cluster, const std::wstring& path,
                         const ScanOptions& options, FlatTree& out,
                         std::uint64_t& excludedTotal, std::uint64_t& orphanTotal,
                         std::uint64_t& unknownSizes) {
    // Records are sequential; a direct index is a few MB and never rehashes.
    std::uint32_t highest = ws::ntfs::kRootRecord;
    for (const auto& entry : entries)
        highest = std::max(highest, std::max(ws::ntfs::recordOf(entry.id), ws::ntfs::recordOf(entry.parent)));
    for (const auto& pair : baseSizes) highest = std::max(highest, pair.first);

    constexpr std::uint32_t kRecordLimit = 64u * 1024u * 1024u;
    const bool haveRecordTable = highest < kRecordLimit;
    const std::uint64_t kNoSize = ~std::uint64_t{0};
    std::vector<std::int32_t> nodeOf;
    std::vector<std::uint64_t> sizeByRecord;
    if (haveRecordTable) {
        nodeOf.assign(static_cast<std::size_t>(highest) + 1, -1);
        sizeByRecord.assign(static_cast<std::size_t>(highest) + 1, kNoSize);
    }
    for (const auto& pair : baseSizes)
        if (haveRecordTable && pair.first < sizeByRecord.size()) sizeByRecord[pair.first] = pair.second;
    // The sizes live in the record table now; the pairs need not survive the
    // rest of the build.
    std::vector<std::pair<std::uint32_t, std::uint64_t>>().swap(baseSizes);

    const int entryCount = static_cast<int>(entries.size());
    for (int i = 0; i < entryCount; ++i) {
        const auto record = ws::ntfs::recordOf(entries[static_cast<std::size_t>(i)].id);
        if (haveRecordTable && record < nodeOf.size()) nodeOf[static_cast<std::size_t>(record)] = i;
    }

    // CSR over entryCount+1: the last slot is a pseudo root that holds every
    // entry whose parent record is missing (or is the volume root).
    const int rootNode = entryCount;
    std::vector<std::int32_t> parentOf(static_cast<std::size_t>(entryCount), rootNode);
    orphanTotal = 0;
    for (int i = 0; i < entryCount; ++i) {
        const auto parent = ws::ntfs::recordOf(entries[static_cast<std::size_t>(i)].parent);
        const bool atRoot = parent == ws::ntfs::kRootRecord;
        const auto parentIndex = (haveRecordTable && parent < nodeOf.size())
            ? nodeOf[static_cast<std::size_t>(parent)] : -1;
        if (parentIndex >= 0 && parentIndex != i) parentOf[static_cast<std::size_t>(i)] = parentIndex;
        else if (!atRoot) ++orphanTotal; // a name whose parent record does not exist
    }
    std::vector<std::int32_t> childStart(static_cast<std::size_t>(entryCount) + 2, 0);
    for (int i = 0; i < entryCount; ++i)
        ++childStart[static_cast<std::size_t>(parentOf[static_cast<std::size_t>(i)]) + 1];
    for (std::size_t i = 1; i < childStart.size(); ++i) childStart[i] += childStart[i - 1];
    std::vector<std::int32_t> cursor = childStart;
    std::vector<std::int32_t> childList(static_cast<std::size_t>(entryCount), -1);
    for (int i = 0; i < entryCount; ++i) {
        const auto parent = parentOf[static_cast<std::size_t>(i)];
        childList[static_cast<std::size_t>(cursor[static_cast<std::size_t>(parent)]++)] = i;
    }

    out = FlatTree{};
    out.names.reserve(entryCount + 1);
    out.parents.reserve(entryCount + 1);
    out.sizes.reserve(entryCount + 1);
    out.allocs.reserve(entryCount + 1);
    out.modified.reserve(entryCount + 1);
    out.attributes.reserve(entryCount + 1);
    out.fileCounts.reserve(entryCount + 1);
    out.dirCounts.reserve(entryCount + 1);
    out.flags.reserve(entryCount + 1);

    out.names.append(QString::fromStdWString(path));
    out.parents.append(-1);
    out.sizes.append(0);
    out.allocs.append(0);
    out.modified.append(0);
    out.attributes.append(FILE_ATTRIBUTE_DIRECTORY);
    out.fileCounts.append(0);
    out.dirCounts.append(0);
    out.flags.append(1);

    std::vector<detail::Frame> stack;
    stack.push_back({rootNode, 0, path, 0});
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(entryCount), 0);
    while (!stack.empty()) {
        detail::Frame& frame = stack.back();
        const int first = childStart[static_cast<std::size_t>(frame.node)];
        const int last = childStart[static_cast<std::size_t>(frame.node) + 1];
        if (first + frame.childPos >= last) { stack.pop_back(); continue; }
        const int child = childList[static_cast<std::size_t>(first + frame.childPos)];
        ++frame.childPos;
        if (child < 0 || visited[static_cast<std::size_t>(child)]) continue;
        visited[static_cast<std::size_t>(child)] = 1;
        auto& entry = entries[static_cast<std::size_t>(child)];
        std::wstring full = frame.path;
        if (!full.empty() && full.back() != L'\\') full += L'\\';
        full += entry.name;
        if (detail::excludedPath(full, options)) { ++excludedTotal; continue; }
        const bool dir = entry.directory;
        std::uint64_t logical = entry.known ? entry.bytes : 0;
        if (!entry.known && !dir) {
            const auto record = ws::ntfs::recordOf(entry.id);
            if (haveRecordTable && record < sizeByRecord.size() && sizeByRecord[record] != kNoSize)
                logical = sizeByRecord[record];
            else
                ++unknownSizes;
        }
        const std::uint64_t alloc = dir ? 0 : diskview::detail::clusterRound(logical, cluster);
        out.names.append(QString::fromStdWString(entry.name));
        out.parents.append(frame.newIndex);
        out.sizes.append(static_cast<qint64>(logical));
        out.allocs.append(static_cast<qint64>(alloc));
        const quint32 stamp = unixSecondsFromFileTime(entry.modified);
        out.modified.append(stamp);
        out.attributes.append(entry.attributes);
        out.fileCounts.append(0);
        out.dirCounts.append(0);
        out.flags.append(static_cast<quint8>((dir ? 1 : 0) | (stamp ? 2 : 0)));
        const int newIndex = static_cast<int>(out.names.size()) - 1;
        std::wstring().swap(entry.name); // hand the buffer back as we go
        stack.push_back({child, newIndex, std::move(full), 0});
    }

    // Every entry is reachable: a missing parent became a root child, so no
    // second "unvisited" pass is needed; orphanTotal already counted those.
    std::deque<mft::Entry>().swap(entries); // names are in the output now
    std::vector<detail::Frame>().swap(stack);
    std::vector<std::int32_t>().swap(parentOf);
    std::vector<std::int32_t>().swap(childStart);
    std::vector<std::int32_t>().swap(childList);
    std::vector<std::int32_t>().swap(cursor);
    std::vector<std::uint8_t>().swap(visited);
    std::vector<std::uint64_t>().swap(sizeByRecord);
    std::vector<std::int32_t>().swap(nodeOf);

    // Subtree totals, one backward pass (parents precede children).
    for (int i = static_cast<int>(out.names.size()) - 1; i >= 1; --i) {
        const int parent = out.parents.at(i);
        if (parent < 0) continue;
        if (out.flags.at(i) & 1) {
            out.dirCounts[parent] += 1 + out.dirCounts.at(i);
            out.fileCounts[parent] += out.fileCounts.at(i);
        } else {
            out.fileCounts[parent] += 1;
        }
        out.sizes[parent] += out.sizes.at(i);
        out.allocs[parent] += out.allocs.at(i);
    }
    return Outcome::Complete;
}

// Reads the MFT, then writes the model's flat arrays directly.
inline Outcome scanToFlat(const std::wstring& path, const ProgressFn& progress,
                          const std::atomic<bool>& cancel, FlatTree& out,
                          std::wstring& reason, const ScanOptions& options,
                          std::uint64_t& fileTotal, std::uint64_t& dirTotal,
                          std::uint64_t& excludedTotal, std::uint64_t& orphanTotal,
                          std::uint64_t& unknownSizes) {
    if (cancel) return Outcome::Cancelled;
    if (!mft::eligible(path)) {
        reason = L"Fast scan requires a fixed NTFS drive root";
        return Outcome::Fallback;
    }
    ws::ntfs::MftReader reader(path.substr(0, 2));
    if (!reader.valid()) { reason = reader.error(); return Outcome::Fallback; }

    std::deque<mft::Entry> entries;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> baseSizes;
    std::uint32_t cluster = reader.geometry().clusterSize;
    std::uint64_t seenFiles = 0, seenBytes = 0;
    std::wstring error;
    bool stopped = false;
    const bool ok = mft::read(reader, cluster, [&](mft::Batch& batch) {
        while (options.paused && *options.paused && !cancel)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (cancel) return false;
        cluster = batch.cluster;
        const auto done = batch.done, total = batch.total;
        appendBatch(batch, entries, baseSizes, seenFiles, seenBytes);
        ScanProgress pg;
        pg.files = seenFiles; pg.bytes = seenBytes; pg.current = path;
        pg.fast = true; pg.recordsDone = done; pg.recordsTotal = total;
        if (progress && !progress(pg)) { stopped = true; return false; }
        return true;
    }, error);
    if (cancel || stopped) return Outcome::Cancelled;
    if (!ok) { reason = error.empty() ? L"MFT scan failed" : error; return Outcome::Fallback; }

    const auto built = buildFlat(entries, std::move(baseSizes), cluster, path, options, out,
                                 excludedTotal, orphanTotal, unknownSizes);
    if (built != Outcome::Complete) return built;
    fileTotal = out.fileCounts.value(0);
    dirTotal = out.dirCounts.value(0);
    return Outcome::Complete;
}

// The read can still fail while the output arrays grow. That is a memory
// failure, not a reason to run a second full scan.
inline Outcome scanToFlatSafe(const std::wstring& path, const ProgressFn& progress,
                              const std::atomic<bool>& cancel, FlatTree& out,
                              std::wstring& reason, const ScanOptions& options,
                              std::uint64_t& fileTotal, std::uint64_t& dirTotal,
                              std::uint64_t& excludedTotal, std::uint64_t& orphanTotal,
                              std::uint64_t& unknownSizes) {
    try {
        return scanToFlat(path, progress, cancel, out, reason, options, fileTotal,
                          dirTotal, excludedTotal, orphanTotal, unknownSizes);
    } catch (const std::bad_alloc&) {
        out = FlatTree{};
        reason = L"MFT fast scan exceeded available memory while building the directory index";
        return Outcome::MemoryFailure;
    }
}

} // namespace diskview::fastscan
