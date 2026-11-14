// In-process, read-only MFT scan using the token supplied by the host.
//
// The QML window wants the flat arrays of ScanTree.h, not the nested Node tree
// the native edition draws. Building Node first and flattening it means two
// complete trees and a hash index per record live at once; on a full C: volume
// under the host's 2 GB job cap that peak turned a finished read into
// bad_alloc, and the old fallback then ran a second (ordinary) scan on top.
//
// The read is accumulated into compact per-node columns and one contiguous
// UTF-16 name buffer. An mft::Entry is about 72 bytes plus a heap string; a
// compact node is about 23 bytes plus its characters, and the characters live
// in one allocation. Names arrive from the MFT in arbitrary record order, so
// the parent lookup is either a direct record-indexed table or, when the MFT is
// sparse, a sorted (record, node) vector searched with lower_bound. Speed is
// traded for the memory that decides whether a system volume fits at all.
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

// The compact per-node columns. Names are characters, addressed by the end
// offset of each node; the slice is the previous end to this one.
struct Nodes {
    std::vector<quint32> record;
    std::vector<qint32> parent;
    std::vector<quint64> logical;
    std::vector<quint32> modified; // Unix seconds
    std::vector<quint16> attributes;
    std::vector<quint8> flags;
    std::vector<quint32> nameEnd;
    std::vector<QChar> chars;
    // Base-record sizes for names that live in an extension record; one pair
    // per record, freed after the lookup table is built.
    std::vector<std::pair<quint32, quint64>> baseSizes;

    int size() const { return static_cast<int>(record.size()); }
    QStringView nameAt(int index) const {
        const quint32 begin = index == 0 ? 0 : nameEnd[static_cast<std::size_t>(index) - 1];
        const quint32 end = nameEnd[static_cast<std::size_t>(index)];
        return QStringView(chars.data() + begin, static_cast<qsizetype>(end - begin));
    }
};

// Turn one read batch into compact nodes, consuming the names.
inline void appendBatch(mft::Batch& batch, Nodes& nodes,
                        std::uint64_t& files, std::uint64_t& bytes) {
    for (auto& entry : batch.entries) {
        if (entry.name.empty()) {
            if (entry.known) nodes.baseSizes.emplace_back(ws::ntfs::recordOf(entry.id), entry.bytes);
            continue; // the redundant per-record carrier
        }
        if (!entry.directory) {
            ++files;
            if (entry.known) bytes += entry.bytes;
        }
        const quint32 stamp = unixSecondsFromFileTime(entry.modified);
        nodes.record.push_back(ws::ntfs::recordOf(entry.id));
        nodes.parent.push_back(static_cast<qint32>(ws::ntfs::recordOf(entry.parent)));
        nodes.logical.push_back(entry.known ? entry.bytes : 0);
        nodes.modified.push_back(stamp);
        nodes.attributes.push_back(static_cast<quint16>(entry.attributes & 0xFFFFu));
        nodes.flags.push_back(static_cast<quint8>((entry.directory ? 1 : 0) | (stamp ? 2 : 0)));
        for (const wchar_t ch : entry.name) nodes.chars.push_back(QChar(static_cast<char16_t>(ch)));
        nodes.nameEnd.push_back(static_cast<quint32>(nodes.chars.size()));
    }
    batch.entries.clear();
}

namespace detail {
struct Frame {
    int node = 0;       // compact node index, or rootNode for the pseudo root
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

// record -> node index. Direct when the MFT is dense; sorted when it is sparse,
// so a high record number never forces a huge table.
class RecordLookup {
public:
    void build(const Nodes& nodes, quint32 highest) {
        if (highest <= quint32(nodes.size()) * 2u) {
            m_direct = true;
            m_byRecord.assign(static_cast<std::size_t>(highest) + 1, -1);
            for (int i = 0; i < nodes.size(); ++i)
                m_byRecord[nodes.record[static_cast<std::size_t>(i)]] = i;
            return;
        }
        m_direct = false;
        m_sorted.reserve(nodes.record.size());
        for (int i = 0; i < nodes.size(); ++i)
            m_sorted.emplace_back(nodes.record[static_cast<std::size_t>(i)], i);
        std::sort(m_sorted.begin(), m_sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    int find(quint32 record) const {
        if (m_direct)
            return record < m_byRecord.size() ? m_byRecord[record] : -1;
        auto it = std::lower_bound(m_sorted.begin(), m_sorted.end(), record,
                                   [](const auto& a, quint32 value) { return a.first < value; });
        int found = -1;
        while (it != m_sorted.end() && it->first == record) { found = it->second; ++it; }
        return found;
    }

private:
    bool m_direct = false;
    std::vector<std::int32_t> m_byRecord;
    std::vector<std::pair<quint32, std::int32_t>> m_sorted;
};
} // namespace detail

// Builds the model's parallel arrays from the compact nodes. Kept separate
// from the reader so ordering, exclusion and totals can be unit tested without
// a volume.
inline Outcome buildFlat(Nodes& nodes, std::uint32_t cluster, const std::wstring& path,
                         const ScanOptions& options, FlatTree& out,
                         std::uint64_t& excludedTotal, std::uint64_t& orphanTotal,
                         std::uint64_t& unknownSizes) {
    const int count = nodes.size();
    quint32 highest = ws::ntfs::kRootRecord;
    for (const auto record : nodes.record) highest = std::max(highest, record);
    for (const auto parent : nodes.parent) highest = std::max(highest, quint32(std::max(0, parent)));
    for (const auto& pair : nodes.baseSizes) highest = std::max(highest, pair.first);

    // A name from an extension record may not repeat its size; look it up in
    // the base records. Sorted, so this costs no record-sized table.
    std::sort(nodes.baseSizes.begin(), nodes.baseSizes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto baseSizeOf = [&nodes](quint32 record, bool* found) {
        auto it = std::lower_bound(nodes.baseSizes.begin(), nodes.baseSizes.end(), record,
                                   [](const auto& a, quint32 value) { return a.first < value; });
        if (it != nodes.baseSizes.end() && it->first == record) { *found = true; return it->second; }
        *found = false;
        return std::uint64_t{0};
    };

    detail::RecordLookup lookup;
    lookup.build(nodes, highest);

    const int rootNode = count;
    std::vector<std::int32_t> parentOf(static_cast<std::size_t>(count), rootNode);
    orphanTotal = 0;
    for (int i = 0; i < count; ++i) {
        const qint32 parentRecord = nodes.parent[static_cast<std::size_t>(i)];
        if (parentRecord == static_cast<qint32>(ws::ntfs::kRootRecord)) continue;
        const int parentIndex = parentRecord >= 0 ? lookup.find(static_cast<quint32>(parentRecord)) : -1;
        if (parentIndex >= 0 && parentIndex != i) parentOf[static_cast<std::size_t>(i)] = parentIndex;
        else ++orphanTotal;
    }
    std::vector<std::int32_t> childStart(static_cast<std::size_t>(count) + 2, 0);
    for (int i = 0; i < count; ++i)
        ++childStart[static_cast<std::size_t>(parentOf[static_cast<std::size_t>(i)]) + 1];
    for (std::size_t i = 1; i < childStart.size(); ++i) childStart[i] += childStart[i - 1];
    std::vector<std::int32_t> cursor = childStart;
    std::vector<std::int32_t> childList(static_cast<std::size_t>(count), -1);
    for (int i = 0; i < count; ++i) {
        const auto parent = parentOf[static_cast<std::size_t>(i)];
        childList[static_cast<std::size_t>(cursor[static_cast<std::size_t>(parent)]++)] = i;
    }

    out = FlatTree{};
    out.names.reserve(count + 1);
    out.names.reserveChars(static_cast<qsizetype>(nodes.chars.size()));
    out.parents.reserve(count + 1);
    out.sizes.reserve(count + 1);
    out.allocs.reserve(count + 1);
    out.modified.reserve(count + 1);
    out.attributes.reserve(count + 1);
    out.fileCounts.reserve(count + 1);
    out.dirCounts.reserve(count + 1);
    out.flags.reserve(count + 1);

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
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(count), 0);
    while (!stack.empty()) {
        detail::Frame& frame = stack.back();
        const int first = childStart[static_cast<std::size_t>(frame.node)];
        const int last = childStart[static_cast<std::size_t>(frame.node) + 1];
        if (first + frame.childPos >= last) { stack.pop_back(); continue; }
        const int child = childList[static_cast<std::size_t>(first + frame.childPos)];
        ++frame.childPos;
        if (child < 0 || visited[static_cast<std::size_t>(child)]) continue;
        visited[static_cast<std::size_t>(child)] = 1;
        const QStringView name = nodes.nameAt(child);
        const bool dir = (nodes.flags[static_cast<std::size_t>(child)] & 1) != 0;
        // Only directories can match an excluded folder, and only directories
        // pass their path on to children. Building a path for every file was
        // millions of string allocations for nothing.
        std::wstring full;
        if (dir) {
            full = frame.path;
            if (!full.empty() && full.back() != L'\\') full += L'\\';
            full.reserve(full.size() + name.size());
            for (const QChar ch : name) full.push_back(static_cast<wchar_t>(ch.unicode()));
            if (detail::excludedPath(full, options)) { ++excludedTotal; continue; }
        }
        std::uint64_t logical = nodes.logical[static_cast<std::size_t>(child)];
        if (logical == 0 && !dir) {
            bool found = false;
            const auto fromBase = baseSizeOf(nodes.record[static_cast<std::size_t>(child)], &found);
            if (found) logical = fromBase;
            else ++unknownSizes;
        }
        const std::uint64_t alloc = dir ? 0 : diskview::detail::clusterRound(logical, cluster);
        out.names.appendChars(name.data(), name.size());
        out.parents.append(frame.newIndex);
        out.sizes.append(static_cast<qint64>(logical));
        out.allocs.append(static_cast<qint64>(alloc));
        out.modified.append(static_cast<qint64>(nodes.modified[static_cast<std::size_t>(child)]));
        out.attributes.append(nodes.attributes[static_cast<std::size_t>(child)]);
        out.fileCounts.append(0);
        out.dirCounts.append(0);
        out.flags.append(nodes.flags[static_cast<std::size_t>(child)]);
        const int newIndex = static_cast<int>(out.names.size()) - 1;
        stack.push_back({child, newIndex, std::move(full), 0});
    }

    // Release the source columns before the totals pass; the output already
    // owns the names.
    std::vector<QChar>().swap(nodes.chars);
    std::vector<std::pair<quint32, quint64>>().swap(nodes.baseSizes);
    std::vector<detail::Frame>().swap(stack);
    std::vector<std::int32_t>().swap(parentOf);

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

// Convenience for tests: turn raw entries into the compact columns.
inline Nodes nodesFromEntries(std::vector<mft::Entry> entries) {
    Nodes nodes;
    mft::Batch batch;
    batch.entries = std::move(entries);
    std::uint64_t files = 0, bytes = 0;
    appendBatch(batch, nodes, files, bytes);
    return nodes;
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

    Nodes nodes;
    nodes.chars.reserve(1 << 20);
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
        appendBatch(batch, nodes, seenFiles, seenBytes);
        ScanProgress pg;
        pg.files = seenFiles; pg.bytes = seenBytes; pg.current = path;
        pg.fast = true; pg.recordsDone = done; pg.recordsTotal = total;
        if (progress && !progress(pg)) { stopped = true; return false; }
        return true;
    }, error);
    if (cancel || stopped) return Outcome::Cancelled;
    if (!ok) { reason = error.empty() ? L"MFT scan failed" : error; return Outcome::Fallback; }

    const auto built = buildFlat(nodes, cluster, path, options, out,
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
