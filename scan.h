// scan.h - the directory scanner. Windows-only, but its result type (tree.h)
// is not: the tests run this scanner against a real temporary directory and
// check the sums, counts and the largest-file list against what they wrote.
//
// What it guarantees:
//   * bounded directory workers using FindFirstFileExW (large fetch), never opening a file:
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
//   * top 1000 largest files with their full paths, for the side panel.

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

// One fixed drive and its volume-level numbers. This is two system calls per
// drive, not a scan: nothing is enumerated, so it is safe to answer while the
// user is still typing.
struct Drive {
    wchar_t letter = 0;     // 'C'
    std::wstring root;      // "C:\\"
    std::uint64_t total = 0;
    std::uint64_t free = 0;
    std::uint64_t used() const noexcept { return total >= free ? total - free : 0; }
};

// The fixed drives of this machine, in drive-letter order. Network, removable
// and optical drives are left out: a treemap of them is a long wait at best,
// and the plugin is about local folders.
inline std::vector<Drive> listFixedDrives()
{
    std::vector<Drive> drives;
    DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i, mask >>= 1) {
        if (!(mask & 1))
            continue;
        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        const std::wstring root = std::wstring(1, letter) + L":\\";
        if (::GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
            continue;
        ULARGE_INTEGER total {}, free {};
        if (!::GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, &free))
            continue; // no medium, or the drive went away between the two calls
        drives.push_back(Drive {letter, root, total.QuadPart, free.QuadPart});
    }
    return drives;
}

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
    std::vector<LargeFile> largest;      // the top 1000, largest first
    std::uint64_t elapsedMs = 0;
    bool cancelled = false;
    bool fast = false;
    std::uint32_t unknownSizes = 0, orphanRecords = 0;
    std::uint32_t pendingRecords = 0, excludedItems = 0;
    unsigned workers = 1;
    std::uint64_t workerMs = 0, mergeMs = 0, snapshotMs = 0;
    std::wstring fallbackReason;
    std::string error; // fatal (root unreadable, path empty); UTF-8
};

struct ScanProgress {
    std::uint64_t files = 0;
    std::uint64_t dirs = 0;
    std::uint64_t bytes = 0;
    std::wstring current; // the directory being read now
    bool fast = false;
    std::uint64_t recordsDone = 0, recordsTotal = 0;
    std::wstring fallbackReason;
};

struct ScanOptions {
    enum class Media { Automatic, Ssd, Hdd, Network };
    unsigned workers = 0; // 0: choose a conservative starting point for this device.
    Media media = Media::Automatic;
    const std::atomic<bool>* paused = nullptr;
    std::vector<std::wstring> excludedPaths;
    unsigned previewIntervalMs = 1500;
};

// Return false to cancel.
using ProgressFn = std::function<bool(const ScanProgress&)>;
using SnapshotFn = std::function<void(std::shared_ptr<ScanResult>)>;

// Trims quotes and spaces, maps a bare drive letter to its root, strips the
// trailing backslash (except on roots), and returns the display form while
// writing the \\?\-prefixed form for the file system calls. Empty on failure.
inline std::wstring normalizePath(std::wstring path, std::wstring* prefixed)
{
    if (prefixed)
        prefixed->clear();
    // trim spaces and quotes
    const auto isPad = [](wchar_t c) { return c == L' ' || c == L'"' || c == L'\t'; };
    const auto trim = [&isPad](std::wstring& p) {
        while (!p.empty() && isPad(p.front()))
            p.erase(p.begin());
        while (!p.empty() && isPad(p.back()))
            p.pop_back();
    };
    trim(path);
    if (path.empty())
        return {};
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0)
        path = L"\\\\" + path.substr(8);
    else if (path.rfind(L"\\\\?\\", 0) == 0)
        path.erase(0, 4);
    // A drive letter on its own means the drive root.
    if (path.size() == 2 && path[1] == L':')
        path += L'\\';
    // Strip trailing backslashes, but keep the root's own ("C:\" or "\\").
    while (path.size() > 1 && path.back() == L'\\'
           && !(path.size() == 3 && path[1] == L':'))
        path.pop_back();
    // Removing the separator can expose padding again ("D:\dir \ " -> "D:\dir").
    trim(path);
    if (path.empty())
        return {};
    // Resolve relative paths and dot segments before adding the extended
    // prefix: Windows does not normalize them once that prefix is present.
    const DWORD needed = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed)
        return {};
    std::wstring absolute(needed, L'\0');
    const DWORD written = ::GetFullPathNameW(path.c_str(), needed, absolute.data(), nullptr);
    if (!written || written >= needed)
        return {};
    absolute.resize(written);
    path = std::move(absolute);
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
    ScanResult* result = nullptr;
    const SnapshotFn* snapshot = nullptr;
    std::uint64_t nextSnapshot = 0;
};

constexpr std::size_t kMaxSkippedList = 32;
constexpr std::size_t kMaxLargest = 1000;
constexpr int kMaxDepth = 96;

struct FindHandle {
    HANDLE value;
    explicit FindHandle(HANDLE handle) : value(handle) {}
    ~FindHandle() { ::FindClose(value); }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;
};

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
    if (*ctx.cancel)
        return false;
    if (!*ctx.progress)
        return true;
    const std::uint64_t now = ::GetTickCount64();
    if (now - ctx.lastPostMs < 100)
        return true;
    ctx.lastPostMs = now;
    ctx.pg.current = displayDir;
    ctx.pg.files = ctx.files;
    ctx.pg.dirs = ctx.dirs;
    ctx.pg.bytes = ctx.bytes;
    if (ctx.snapshot && *ctx.snapshot && now >= ctx.nextSnapshot) {
        auto copy = std::make_shared<ScanResult>(*ctx.result);
        const auto sum = [&](const auto& self, Node& n) -> void {
            if (!n.dir) return;
            n.logical=0; n.allocApprox=0; n.files=0; n.dirs=0;
            for (auto& c:n.children) {
                self(self,c); n.logical+=c.logical; n.allocApprox+=c.allocApprox;
                n.files+=c.dir?c.files:1; n.dirs+=c.dir?c.dirs+1:0;
            }
            std::sort(n.children.begin(),n.children.end(),[](const Node& a,const Node& b){return a.logical>b.logical;});
        };
        sum(sum,copy->root); copy->files=ctx.files;copy->dirs=ctx.dirs;
        copy->skippedDirs=ctx.skippedDirs;copy->skippedList=ctx.skippedList;
        auto ranking=ctx.top;
        while(!ranking.empty()){copy->largest.push_back(ranking.top());ranking.pop();}
        std::sort(copy->largest.begin(),copy->largest.end(),std::greater<>());
        (*ctx.snapshot)(std::move(copy));
        // Full immutable snapshots are adaptive: keep their cost below ~10% of scan time.
        const auto spent=::GetTickCount64()-now;
        ctx.nextSnapshot=::GetTickCount64()+std::max<std::uint64_t>(1000,spent*10);
    }
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
    HANDLE h = ::FindFirstFileExW(childPath(prefixedDir, L"*").c_str(), FindExInfoBasic, &fd,
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
    const FindHandle ownedHandle(h);
    std::uint64_t selfLogical = 0, selfAlloc = 0;
    std::uint32_t selfFiles = 0, selfDirs = 0;
    bool ok = true;
    for (;;) {
        if (*ctx.cancel || !report(ctx, displayDir)) {
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
            const std::uint64_t modified =
                (static_cast<std::uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
            if (isDir && reparse) {
                // A junction or a directory symlink: shown as an entry of zero
                // size, never followed (no loops, no double-counting).
                ++ctx.skippedLinks;
                ++ctx.dirs;
                ++selfDirs;
                Node link;
                link.name = n;
                link.dir = true;
                link.skippedChildren = 0;
                link.attributes = fd.dwFileAttributes;
                link.modified = modified;
                node.children.push_back(std::move(link));
            } else if (isDir) {
                ++ctx.dirs;
                node.children.emplace_back();
                Node& d = node.children.back();
                d.name = n;
                d.dir = true;
                d.attributes = fd.dwFileAttributes;
                d.modified = modified;
                const bool childDone = walkDir(childPath(prefixedDir, n), childPath(displayDir, n), d, depth + 1, ctx, out);
                selfLogical += d.logical;
                selfAlloc += d.allocApprox;
                selfFiles += d.files;
                selfDirs += d.dirs + 1;
                node.skippedChildren += d.skippedChildren;
                if (!childDone) { ok=false; break; }
            } else {
                ++ctx.files;
                ctx.bytes += logical;
                Node f;
                f.name = n;
                f.dir = false;
                f.logical = logical;
                f.allocApprox = clusterRound(logical, ctx.clusterBytes);
                f.attributes = fd.dwFileAttributes;
                f.modified = modified;
                selfLogical += logical;
                selfAlloc += f.allocApprox;
                selfFiles += 1;
                considerLarge(ctx, logical, displayDir, n);
                node.children.push_back(std::move(f));
            }
        }
        if (!::FindNextFileW(h, &fd)) {
            const DWORD e = ::GetLastError();
            if (e != ERROR_NO_MORE_FILES) {
                // The listing broke mid-directory: record it, keep what we have.
                node.skippedChildren += 1;
                recordSkipped(ctx, displayDir, e);
            }
            break;
        }
    }
    node.logical += selfLogical;
    node.allocApprox += selfAlloc;
    node.files += selfFiles;
    node.dirs += selfDirs;
    std::stable_sort(node.children.begin(), node.children.end(),
                     [](const Node& a, const Node& b) { return a.logical > b.logical; });
    return ok;
}

} // namespace detail

// Scans `path`. On a filesystem failure sets result->error and leaves
// the tree partial. Cancellation is checked between entries and inside the
// progress callback; a cancelled scan sets result->cancelled and keeps what it
// gathered. Callers must label this as incomplete when displaying it.
inline void scanPathSerial(const std::wstring& path, const ProgressFn& progress,
                     const std::atomic<bool>& cancel, ScanResult* result, const SnapshotFn& snapshot = {})
{
    *result = ScanResult {};
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
    ctx.result=result;ctx.snapshot=&snapshot;ctx.nextSnapshot=::GetTickCount64()+1000;
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

#include "parallel_scan.h"

#endif // DISKVIEW_SCAN_H
