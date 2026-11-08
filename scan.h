// scan.h - the directory scanner. Windows-only, but its result type (tree.h)
// is not: the tests run this scanner against a real temporary directory and
// check the sums, counts and the largest-file list against what they wrote.
//
// What it guarantees:
//   * depth-first with FindFirstFileExW (large fetch), never opening a file:
//     cloud placeholders are not downloaded, file contents are never touched;
//   * reparse points (junctions, symlinks) are counted, never followed: no
//     loops and no double-counting through links;
//   * a directory that cannot be read is recorded (up to 32 paths, then just
//     counted) - never silently treated as empty;
//   * `logical` is the byte count the file system reports; `allocApprox` is
//     cluster-rounded (an approximation: sparse and compressed files report
//     more than they occupy on disk, and hard-linked files are counted once
//     per link);
//   * cancellation through an atomic checked between entries, plus the
//     progress callback's return value;
//   * top 64 largest files with their full paths, for the side panel.

#ifndef DISKVIEW_SCAN_H
#define DISKVIEW_SCAN_H

#include "tree.h"
#include "winshun.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace diskview {

struct LargeFile {
    std::uint64_t logical = 0;
    std::wstring path; // display path, without the \\?\ prefix
    bool operator>(const LargeFile& o) const { return logical > o.logical; }
};

struct SkippedDir {
    std::wstring path; // display path
    DWORD error = 0;
};

struct ScanResult {
    Node root; // name: the display path of the scan root
    std::wstring displayRoot;
    std::uint64_t clusterBytes = 4096;
    std::uint32_t files = 0;
    std::uint32_t dirs = 0;
    std::uint32_t skippedDirs = 0;
    std::uint32_t skippedLinks = 0;
    std::vector<SkippedDir> skippedList; // the first 32, then only counted
    std::vector<LargeFile> largest;      // the top 64, largest first
    std::uint64_t elapsedMs = 0;
    bool cancelled = false;
    std::string error; // fatal (root unreadable, path empty); UTF-8
};

struct ScanProgress {
    std::uint64_t files = 0;
    std::uint64_t dirs = 0;
    std::uint64_t bytes = 0;
    std::wstring current; // the directory being read now
};

// Return false to cancel.
using ProgressFn = std::function<bool(const ScanProgress&)>;

// Trims quotes and spaces, maps a bare drive letter to its root, strips the
// trailing backslash (except on roots), and returns the display form while
// writing the \\?\-prefixed form for the file system calls. Empty on failure.
inline std::wstring normalizePath(std::wstring path, std::wstring* prefixed)
{
    // trim spaces and quotes
    const auto isPad = [](wchar_t c) { return c == L' ' || c == L'"' || c == L'\t'; };
    while (!path.empty() && isPad(path.front()))
        path.erase(path.begin());
    while (!path.empty() && isPad(path.back()))
        path.pop_back();
    if (path.size() == 2 && path[1] == L':')
        path += L'\\';
    if (path.empty())
        return {};
    // strip a trailing backslash unless this is a root ("C:\" or "\\")
    while (path.size() > 2 && path.back() == L'\\')
        path.pop_back();
    if (prefixed) {
        if (path.rfind(L"\\\\?\\", 0) == 0)
            *prefixed = path;
        else if (path.rfind(L"\\\\", 0) == 0)
            *prefixed = L"\\\\?\\UNC\\" + path.substr(2);
        else
            *prefixed = L"\\\\?\\" + path;
    }
    return path;
}

namespace detail {

struct Ctx {
    const std::atomic<bool>* cancel = nullptr;
    const ProgressFn* progress = nullptr;
    ScanProgress pg;
    std::uint64_t lastPostMs = 0;
    std::uint32_t files = 0, dirs = 0, skippedDirs = 0, skippedLinks = 0;
    std::vector<SkippedDir> skippedList;
    std::priority_queue<LargeFile, std::vector<LargeFile>, std::greater<>> top;
    std::uint64_t clusterBytes = 4096;
    std::uint64_t bytes = 0;
};

constexpr std::size_t kMaxSkippedList = 32;
constexpr std::size_t kMaxLargest = 64;
constexpr int kMaxDepth = 96;

inline void recordSkipped(Ctx& ctx, const std::wstring& path, DWORD error)
{
    ++ctx.skippedDirs;
    if (ctx.skippedList.size() < kMaxSkippedList)
        ctx.skippedList.push_back({path, error});
}

inline void considerLarge(Ctx& ctx, std::uint64_t logical, const std::wstring& displayDir,
                          const wchar_t* name)
{
    if (ctx.top.size() >= kMaxLargest) {
        if (logical <= ctx.top.top().logical)
            return;
        ctx.top.pop();
    }
    std::wstring path = displayDir;
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += name;
    ctx.top.push({logical, std::move(path)});
}

inline bool report(Ctx& ctx, const std::wstring& displayDir)
{
    ctx.pg.current = displayDir;
    ctx.pg.files = ctx.files;
    ctx.pg.dirs = ctx.dirs;
    ctx.pg.bytes = ctx.bytes;
    if (*ctx.cancel)
        return false;
    if (!*ctx.progress)
        return true;
    const std::uint64_t now = ::GetTickCount64();
    if (now - ctx.lastPostMs < 100)
        return true;
    ctx.lastPostMs = now;
    return (*ctx.progress)(ctx.pg);
}

inline std::uint64_t clusterRound(std::uint64_t logical, std::uint64_t cluster)
{
    if (logical == 0 || cluster <= 1)
        return logical;
    return ((logical + cluster - 1) / cluster) * cluster;
}

// Reads one directory into `node`. Returns false when cancelled.
inline bool walkDir(const std::wstring& prefixedDir, const std::wstring& displayDir, Node& node,
                    int depth, Ctx& ctx, ScanResult* out)
{
    if (depth > kMaxDepth) {
        node.skippedChildren += 1;
        recordSkipped(ctx, displayDir, ERROR_CANT_ACCESS_FILE);
        return true;
    }
    WIN32_FIND_DATAW fd {};
    HANDLE h = ::FindFirstFileExW((prefixedDir + L"\\*").c_str(), FindExInfoBasic, &fd,
                                  FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = ::GetLastError();
        if (depth == 0) {
            out->error = "无法读取路径（不存在或没有权限），Windows 错误 " + std::to_string(e)
                         + " / cannot read the path (missing or access denied), Windows error "
                         + std::to_string(e);
            return false;
        }
        node.skippedChildren += 1;
        recordSkipped(ctx, displayDir, e);
        return true;
    }
    std::uint64_t selfLogical = 0, selfAlloc = 0;
    std::uint32_t selfFiles = 0, selfDirs = 0;
    bool ok = true;
    for (;;) {
        if (ctx.cancel || !report(ctx, displayDir)) {
            out->cancelled = true;
            ok = false;
            break;
        }
        const wchar_t* n = fd.cFileName;
        if (!(n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)))) {
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            const std::uint64_t logical =
                (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            if (isDir && reparse) {
                // A junction or a directory symlink: shown as an entry of zero
                // size, never followed (no loops, no double-counting).
                ++ctx.skippedLinks;
                Node link;
                link.name = n;
                link.dir = true;
                link.skippedChildren = 0;
                node.children.push_back(std::move(link));
            } else if (isDir) {
                ++ctx.dirs;
                Node d;
                d.name = n;
                d.dir = true;
                if (!walkDir(prefixedDir + L"\\" + n, displayDir + L"\\" + n, d, depth + 1, ctx, out)) {
                    ok = false;
                    break;
                }
                selfLogical += d.logical;
                selfAlloc += d.allocApprox;
                selfFiles += d.files;
                selfDirs += d.dirs + 1;
                node.children.push_back(std::move(d));
            } else {
                ++ctx.files;
                ctx.bytes += logical;
                Node f;
                f.name = n;
                f.dir = false;
                f.logical = logical;
                f.allocApprox = clusterRound(logical, ctx.clusterBytes);
                selfLogical += logical;
                selfAlloc += f.allocApprox;
                selfFiles += 1;
                considerLarge(ctx, logical, displayDir, n);
                node.children.push_back(std::move(f));
            }
        }
        if (!::FindNextFileW(h, &fd)) {
            const DWORD e = ::GetLastError();
            if (e != ERROR_NO_MORE_FILES && e != ERROR_ACCESS_DENIED) {
                // The listing broke mid-directory: record it, keep what we have.
                node.skippedChildren += 1;
                recordSkipped(ctx, displayDir, e);
            }
            break;
        }
    }
    ::FindClose(h);

    node.logical += selfLogical;
    node.allocApprox += selfAlloc;
    node.files += selfFiles;
    node.dirs += selfDirs;
    std::stable_sort(node.children.begin(), node.children.end(),
                     [](const Node& a, const Node& b) { return a.logical > b.logical; });
    return ok;
}

} // namespace detail

// Scans `path`. Never throws; on a fatal problem sets result->error and leaves
// the tree partial. Cancellation is checked between entries and inside the
// progress callback; a cancelled scan sets result->cancelled and keeps what it
// gathered, which the caller should discard (a partial tree would misreport).
inline void scanPath(const std::wstring& path, const ProgressFn& progress,
                     const std::atomic<bool>& cancel, ScanResult* result)
{
    std::wstring prefixed;
    const std::wstring display = normalizePath(path, &prefixed);
    if (display.empty()) {
        result->error = "路径为空 / the path is empty";
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();

    // Cluster size of the volume that holds the root (for the allocated-size
    // approximation). Network roots and exotic file systems fall back to 4 KB.
    wchar_t root[256] = {};
    if (display.rfind(L"\\\\", 0) == 0) {
        // \\server\share\... -> \\server\share (the share root)
        const std::size_t slash = display.find(L'\\', 2);
        const std::size_t share = display.find(L'\\', slash == std::wstring::npos ? 2 : slash + 1);
        const std::wstring unc = display.substr(0, share == std::wstring::npos ? display.size() : share) + L"\\";
        if (unc.size() < sizeof root / sizeof root[0])
            std::copy(unc.begin(), unc.end(), root);
    } else if (display.size() >= 2 && display[1] == L':') {
        root[0] = display[0];
        root[1] = L':';
        root[2] = L'\\';
    }
    DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
    if (root[0] && ::GetDiskFreeSpaceW(root, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)
        && sectorsPerCluster && bytesPerSector)
        result->clusterBytes = static_cast<std::uint64_t>(sectorsPerCluster) * bytesPerSector;

    result->displayRoot = display;
    result->root = Node {};
    result->root.name = display;
    result->root.dir = true;

    detail::Ctx ctx;
    ctx.cancel = &cancel;
    ctx.progress = &progress;
    ctx.clusterBytes = result->clusterBytes;
    result->root.skippedChildren = 0;
    const bool done = detail::walkDir(prefixed, display, result->root, 0, ctx, result);
    (void)done; // a cancelled walk keeps partial data but sets result->cancelled

    result->files = ctx.files;
    result->dirs = ctx.dirs;
    result->skippedDirs = ctx.skippedDirs;
    result->skippedLinks = ctx.skippedLinks;
    result->skippedList = std::move(ctx.skippedList);
    while (!ctx.top.empty()) {
        result->largest.push_back(ctx.top.top());
        ctx.top.pop();
    }
    std::sort(result->largest.begin(), result->largest.end(), std::greater<>());
    result->elapsedMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
}

} // namespace diskview

#endif // DISKVIEW_SCAN_H
