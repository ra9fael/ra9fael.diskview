// In-process, read-only MFT scan using the token supplied by the host.
//
// The QML window wants the flat arrays of ScanTree.h, not the nested Node tree
// the native edition draws. Building Node first and flattening it means two
// complete trees and a hash index per record live at once; on a full C: volume
// that peak turned a finished read into bad_alloc, and the fallback then ran a
// second (ordinary) scan on top of it. The builder here goes from MFT records
// straight to the model's parallel arrays: one node struct per record, a
// record-indexed parent table, and a CSR child list. No intermediate tree, no
// per-record hash map.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <thread>
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

// Segmented storage never allocates old+new contiguous Entry buffers during
// growth. Batches transfer their names rather than copying them. The sink must
// be mutable: std::move(const Entry&) still copies the string.
inline void appendBatch(mft::Batch& batch, std::deque<mft::Entry>& entries,
                        std::uint64_t& files, std::uint64_t& bytes) {
    for(auto& entry:batch.entries) {
        if(!entry.directory&&!entry.name.empty()) {
            ++files;
            if(entry.known)bytes+=entry.bytes;
        }
        entries.push_back(std::move(entry));
    }
    batch.entries.clear();
}

namespace detail {

struct TempNode {
    std::wstring name;
    std::int32_t parentRecord=-1;
    std::uint32_t record=0;
    std::uint64_t logical=0;
    std::uint64_t modified=0;
    std::uint32_t attributes=0;
    std::uint8_t flags=0; // bit 0 directory, bit 1 has a stamp
};

struct Frame {
    int node=0;
    int newIndex=-1;
    std::wstring path;
    int childPos=0;
};

inline bool excludedPath(const std::wstring& path, const ScanOptions& options) {
    for (const auto& base : options.excludedPaths) {
        if (!base.empty() && diskview::sameOrBelow(path, base)) return true;
    }
    return false;
}

} // namespace detail

inline Outcome buildFlat(std::deque<mft::Entry>& entries, std::uint32_t cluster,
                         const std::wstring& path, const ScanOptions& options,
                         FlatTree& out, std::uint64_t& excludedTotal,
                         std::uint64_t& orphanTotal, std::uint64_t& unknownSizes);

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
        appendBatch(batch, entries, seenFiles, seenBytes);
        ScanProgress pg;
        pg.files = seenFiles; pg.bytes = seenBytes; pg.current = path;
        pg.fast = true; pg.recordsDone = done; pg.recordsTotal = total;
        if (progress && !progress(pg)) { stopped = true; return false; }
        return true;
    }, error);
    if (cancel || stopped) return Outcome::Cancelled;
    if (!ok) { reason = error.empty() ? L"MFT scan failed" : error; return Outcome::Fallback; }

    const auto built = buildFlat(entries, cluster, path, options, out, excludedTotal, orphanTotal, unknownSizes);
    if (built != Outcome::Complete) return built;
    fileTotal = out.fileCounts.value(0);
    dirTotal = out.dirCounts.value(0);
    return Outcome::Complete;
}

// Builds the model's parallel arrays from the MFT records. Kept separate
// from the reader so the ordering, exclusion and totals can be unit tested
// without a volume.
inline Outcome buildFlat(std::deque<mft::Entry>& entries, std::uint32_t cluster,
                         const std::wstring& path, const ScanOptions& options,
                         FlatTree& out, std::uint64_t& excludedTotal,
                         std::uint64_t& orphanTotal, std::uint64_t& unknownSizes) {
    // Record numbers are sequential on a volume; a direct index is a few MB
    // and never rehashes. An implausible number (damaged record) makes that
    // node an orphan child of the root instead of allocating a huge table.
    std::uint32_t highest = ws::ntfs::kRootRecord;
    for (const auto& entry : entries) {
        if (entry.name.empty()) continue;
        const auto id = ws::ntfs::recordOf(entry.id);
        const auto parent = ws::ntfs::recordOf(entry.parent);
        if (id != ws::ntfs::kRootRecord) highest = std::max(highest, id);
        highest = std::max(highest, parent);
    }
    constexpr std::uint32_t kRecordLimit = 64u * 1024u * 1024u;
    std::vector<std::int32_t> nodeOf;
    if (highest < kRecordLimit) nodeOf.assign(static_cast<std::size_t>(highest) + 1, -1);

    std::vector<detail::TempNode> nodes;
    nodes.reserve(entries.size());
    for (auto& entry : entries) {
        if (entry.name.empty()) continue;
        const auto record = ws::ntfs::recordOf(entry.id);
        if (record == ws::ntfs::kRootRecord) continue;
        detail::TempNode node;
        node.name = std::move(entry.name);
        node.record = record;
        node.parentRecord = ws::ntfs::recordOf(entry.parent);
        node.logical = entry.known ? entry.bytes : 0;
        if (!entry.known && !entry.directory) ++unknownSizes;
        node.modified = entry.modified;
        node.attributes = entry.attributes;
        node.flags = static_cast<std::uint8_t>((entry.directory ? 1 : 0) | (entry.modified ? 2 : 0));
        const int index = static_cast<int>(nodes.size());
        nodes.push_back(std::move(node));
        if (!nodeOf.empty() && record < nodeOf.size()) nodeOf[record] = index;
    }
    // The names live in `nodes` now; let the read batches go before the CSR
    // and output arrays are allocated.
    std::deque<mft::Entry>().swap(entries);

    // CSR over nodes.size()+1: the last slot is a pseudo root that holds every
    // node whose parent record is missing (or the volume root).
    const int rootNode = static_cast<int>(nodes.size());
    std::vector<std::int32_t> parentOf(nodes.size(), rootNode);
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const auto p = nodes[static_cast<std::size_t>(i)].parentRecord;
        if (p >= 0 && !nodeOf.empty() && static_cast<std::size_t>(p) < nodeOf.size()) {
            const auto parentIndex = nodeOf[static_cast<std::size_t>(p)];
            if (parentIndex >= 0 && parentIndex != i) parentOf[static_cast<std::size_t>(i)] = parentIndex;
        }
    }
    std::vector<std::int32_t> childStart(nodes.size() + 3, 0);
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        ++childStart[static_cast<std::size_t>(parentOf[static_cast<std::size_t>(i)]) + 1];
    for (std::size_t i = 1; i < childStart.size(); ++i) childStart[i] += childStart[i - 1];
    std::vector<std::int32_t> cursor = childStart;
    std::vector<std::int32_t> childList(nodes.size(), -1);
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const auto parent = parentOf[static_cast<std::size_t>(i)];
        childList[static_cast<std::size_t>(cursor[static_cast<std::size_t>(parent)]++)] = i;
    }

    out = FlatTree{};
    out.names.reserve(static_cast<int>(nodes.size()) + 1);
    out.parents.reserve(out.names.capacity());
    out.sizes.reserve(out.names.capacity());
    out.allocs.reserve(out.names.capacity());
    out.modified.reserve(out.names.capacity());
    out.attributes.reserve(out.names.capacity());
    out.fileCounts.reserve(out.names.capacity());
    out.dirCounts.reserve(out.names.capacity());
    out.flags.reserve(out.names.capacity());

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
    std::vector<std::uint8_t> visited(nodes.size(), 0);
    while (!stack.empty()) {
        detail::Frame& frame = stack.back();
        const int first = childStart[static_cast<std::size_t>(frame.node)];
        const int last = childStart[static_cast<std::size_t>(frame.node) + 1];
        if (first + frame.childPos >= last) { stack.pop_back(); continue; }
        const int child = childList[static_cast<std::size_t>(first + frame.childPos)];
        ++frame.childPos;
        if (child < 0) continue;
        if (visited[static_cast<std::size_t>(child)]) continue;
        visited[static_cast<std::size_t>(child)] = 1;
        auto& node = nodes[static_cast<std::size_t>(child)];
        std::wstring full = frame.path;
        if (!full.empty() && full.back() != L'\\') full += L'\\';
        full += node.name;
        if (detail::excludedPath(full, options)) { ++excludedTotal; continue; }
        const std::uint64_t alloc = (node.flags & 1) ? 0 : diskview::detail::clusterRound(node.logical, cluster);
        out.names.append(QString::fromStdWString(node.name));
        out.parents.append(frame.newIndex);
        out.sizes.append(static_cast<qint64>(node.logical));
        out.allocs.append(static_cast<qint64>(alloc));
        out.modified.append(static_cast<qint64>(node.modified));
        out.attributes.append(node.attributes);
        out.fileCounts.append(0);
        out.dirCounts.append(0);
        out.flags.append(node.flags);
        const int newIndex = static_cast<int>(out.names.size()) - 1;
        std::wstring().swap(node.name);
        stack.push_back({child, newIndex, std::move(full), 0});
    }
    std::vector<detail::Frame>().swap(stack);
    std::vector<detail::TempNode>().swap(nodes);
    std::vector<std::int32_t>().swap(parentOf);
    std::vector<std::int32_t>().swap(childStart);
    std::vector<std::int32_t>().swap(childList);
    std::vector<std::int32_t>().swap(cursor);
    std::vector<std::uint8_t>().swap(visited);
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
    orphanTotal = 0;
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
