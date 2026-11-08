// viewer.h - the Disk View window for the WinShun plugin.
//
// One dedicated GUI thread owns the window and all drawing state; the serve()
// thread only calls openViewerWindow()/closeViewerAndWait() under the state
// mutex. Scans run on their own thread and post WM_APP_* messages, never
// touching GUI state directly. All painting is GDI into a memory bitmap.
// The window follows the host's theme snapshot taken when it opens.
//
// This file must not write to stdout; report problems through `error` and
// winshun::debug (stderr).

#ifndef DISKVIEW_VIEWER_H
#define DISKVIEW_VIEWER_H

#include "scan.h"
#include "scan_helper.h"
#include "tree.h"
#include "winshun.h"

#include <windows.h>
#include "window_icons.h"
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <deque>

namespace diskview {

struct ViewerSpec {
    std::wstring initialPath; // seed the path box and start scanning (user-triggered)
    bool zh = true;
    std::string theme = "dark";   // light | dark, from the host at open time
    std::string dataDir;          // WS_DATA_DIR (UTF-8); diskview.json lives here
    bool defaultSunburst = false; // the defaultView setting
    winshun::Json appearance;      // the host's look (nativeLook and the rest)
};

bool openViewerWindow(const ViewerSpec& spec, std::string& error);
bool viewerVisible();
void closeViewerAndWait(unsigned timeoutMs);
// The host's look changed while the window is open: it takes the new
// colours. False when no window is open.
bool updateViewerAppearance(const winshun::Json& appearance);

// ---- persisted configuration (WS_DATA_DIR/diskview.json) --------------------------

// Only the last scanned path is kept. Which view the window opens with is the
// `defaultView` setting, not something a file may decide.
struct ViewConfig {
    std::wstring lastPath;
    bool hasFile = false; // a config file existed at all
};

inline std::wstring configPath(const std::string& dataDir)
{
    if (dataDir.empty())
        return {};
    return winshun::wide(dataDir) + L"\\diskview.json";
}

inline ViewConfig loadViewConfig(const std::string& dataDir)
{
    ViewConfig c;
    const std::wstring path = configPath(dataDir);
    if (path.empty())
        return c;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        return c;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto j = winshun::Json::parse(text);
    if (j && j->isObject()) {
        if ((*j)["path"].isString())
            c.lastPath = winshun::wide((*j)["path"].toString());
        c.hasFile = true; // a file from before still carries "sunburst"; it is ignored
    }
    return c;
}

inline void saveViewConfig(const std::string& dataDir, const ViewConfig& c)
{
    const std::wstring path = configPath(dataDir);
    if (path.empty())
        return;
    const winshun::Json j(winshun::Json::Object {
        {"path", winshun::utf8(c.lastPath)},
    });
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (f)
        f << j.dump();
}

// ---- detail ------------------------------------------------------------------------

namespace detail {

constexpr UINT WM_APP_PROGRESS = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr UINT WM_APP_APPEARANCE = WM_APP + 3;
constexpr UINT WM_APP_PARTIAL = WM_APP + 4;
constexpr UINT WM_APP_START_SCAN = WM_APP + 5;

struct Palette {
    COLORREF bg, panel, panelText, text, muted, accent, danger, border, hoverLine, progress;
    COLORREF accentFill; // the fill of a chosen button
    COLORREF accentText; // the text on that fill
    COLORREF hover;      // a hovered row or button
    COLORREF input;      // the path box's fill
    bool dark = true;
};

inline COLORREF lighten(COLORREF c, int amount)
{
    const auto up = [amount](int v) { return std::min(255, std::max(0, v + amount)); };
    return RGB(static_cast<BYTE>(up(GetRValue(c))), static_cast<BYTE>(up(GetGValue(c))),
               static_cast<BYTE>(up(GetBValue(c))));
}

inline COLORREF tileText(COLORREF bg)
{
    const double lum = 0.299 * GetRValue(bg) + 0.587 * GetGValue(bg) + 0.114 * GetBValue(bg);
    return lum > 140 ? RGB(24, 26, 30) : RGB(240, 242, 245);
}

inline Palette darkPalette()
{
    const COLORREF accent = RGB(131, 185, 255);
    const COLORREF panel = RGB(32, 38, 48);
    return {RGB(23, 27, 34), panel, RGB(236, 240, 247), RGB(236, 240, 247), RGB(179, 191, 208),
            accent, RGB(244, 202, 136), RGB(59, 69, 85), accent, accent,
            RGB(151, 196, 255), RGB(21, 36, 56), RGB(39, 62, 92), panel, true};
}

inline Palette lightPalette()
{
    const COLORREF accent = RGB(7, 93, 184);
    const COLORREF panel = RGB(255, 255, 255);
    return {RGB(245, 247, 251), panel, RGB(32, 43, 60), RGB(32, 43, 60), RGB(86, 101, 123),
            accent, RGB(128, 83, 27), RGB(221, 228, 238), accent, accent,
            RGB(8, 102, 201), RGB(255, 255, 255), RGB(234, 243, 255), panel, false};
}

// ---- the host's look (appearance()["nativeLook"]) --------------------------------------

// "#AARRGGBB" or "#RRGGBB" as 0xAARRGGBB; 0 when it is not a colour. The host
// writes the alpha first (docs/plugins.md, 外观).
inline std::uint32_t argbOf(const std::string& name)
{
    if ((name.size() != 7 && name.size() != 9) || name[0] != '#')
        return 0;
    std::uint32_t value = 0;
    for (std::size_t i = 1; i < name.size(); ++i) {
        const char c = name[i];
        const int digit = c >= '0' && c <= '9' ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
        if (digit < 0)
            return 0;
        value = value << 4 | static_cast<std::uint32_t>(digit);
    }
    return name.size() == 7 ? 0xFF000000u | value : value;
}

// The host's colour over an opaque one. With the transparency effect on some
// of its colours are half transparent; a window paints on an opaque base, so
// they are laid over it here (docs/plugins.md, 外观).
inline COLORREF over(COLORREF base, std::uint32_t argb)
{
    const int a = static_cast<int>((argb >> 24) & 0xFF);
    const int r = static_cast<int>((argb >> 16) & 0xFF);
    const int g = static_cast<int>((argb >> 8) & 0xFF);
    const int b = static_cast<int>(argb & 0xFF);
    if (a == 255)
        return RGB(static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(b));
    const auto mix = [a](int c, int under) { return (c * a + under * (255 - a) + 127) / 255; };
    return RGB(static_cast<BYTE>(mix(r, GetRValue(base))), static_cast<BYTE>(mix(g, GetGValue(base))),
               static_cast<BYTE>(mix(b, GetBValue(base))));
}

// The window's palette from the host's look: the colours of Win顺's own
// interface style, so the window looks like Win顺 (docs/plugins.md, 外观).
// A colour the host did not send keeps the built-in one; a host too old to
// send a look at all keeps the whole built-in palette.
inline Palette hostPalette(const winshun::Json& appearance, const Palette& fallback)
{
    const winshun::Json& native = appearance["nativeLook"];
    if (!native.isObject())
        return fallback;
    if (native["dark"].isBool() && native["dark"].toBool() != fallback.dark)
        return fallback; // written for the other theme, a moment before it changes

    Palette p = fallback;
    const COLORREF base = fallback.dark ? RGB(0, 0, 0) : RGB(255, 255, 255);
    std::uint32_t argb = argbOf(native["background"].toString());
    const COLORREF bg = argb ? over(base, argb) : fallback.bg;
    p.bg = bg;
    argb = argbOf(native["layer"].toString());
    if (!argb)
        argb = argbOf(native["page"].toString());
    p.panel = argb ? over(bg, argb) : fallback.panel;
    argb = argbOf(native["text"].toString());
    p.text = argb ? over(bg, argb) : fallback.text;
    p.panelText = p.text;
    argb = argbOf(native["subtext"].toString());
    if (!argb)
        argb = argbOf(native["faint"].toString());
    p.muted = argb ? over(bg, argb) : fallback.muted;
    argb = argbOf(native["accent"].toString());
    p.accent = argb ? over(bg, argb) : fallback.accent;
    argb = argbOf(native["accentFill"].toString());
    p.accentFill = argb ? over(bg, argb) : fallback.accentFill;
    argb = argbOf(native["accentText"].toString());
    p.accentText = argb ? over(p.accentFill, argb) : fallback.accentText;
    argb = argbOf(native["danger"].toString());
    p.danger = argb ? over(bg, argb) : fallback.danger;
    argb = argbOf(native["divider"].toString());
    if (!argb)
        argb = argbOf(native["controlBorder"].toString());
    p.border = argb ? over(bg, argb) : fallback.border;
    argb = argbOf(native["hover"].toString());
    p.hover = argb ? over(p.panel, argb) : fallback.hover;
    argb = argbOf(native["control"].toString());
    if (!argb)
        argb = argbOf(native["inputFocused"].toString());
    p.input = argb ? over(bg, argb) : fallback.input;
    p.hoverLine = p.accent;
    p.progress = p.accent;
    return p;
}

inline COLORREF hsl(int hue, int sat, int light)
{
    const double h = (hue % 360 + 360) % 360 / 360.0;
    const double s = sat / 100.0, l = light / 100.0;
    const auto f = [&](double n) {
        const double k = std::fmod(n + h * 12.0, 12.0);
        const double a = s * std::min(l, 1.0 - l);
        const double v = l - a * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0}));
        return std::clamp(v, 0.0, 1.0);
    };
    return RGB(static_cast<BYTE>(f(0) * 255), static_cast<BYTE>(f(8) * 255), static_cast<BYTE>(f(4) * 255));
}

inline COLORREF dirColor(int depth, const Palette& p)
{
    return lighten(p.dark?RGB(41,73,67):RGB(214,234,230),depth*(p.dark?7:-5));
}

// Extension categories, WizTree-style; unlisted extensions hash to a stable
// hue so colors stay consistent between frames and rescans.
inline int extHue(const std::wstring& name)
{
    std::wstring ext;
    const std::size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos)
        ext = name.substr(dot + 1);
    for (wchar_t& c : ext)
        c = static_cast<wchar_t>(::towlower(c));
    static const struct {
        const wchar_t* exts;
        int hue;
    } groups[] = {
        {L"exe dll sys msi scr com", 275},
        {L"zip rar 7z tar gz bz2 xz zst iso img vhd vhdx wim cab", 45},
        {L"mp4 mkv avi mov wmv flv webm mp3 wav flac ogg m4a", 275},
        {L"jpg jpeg png gif bmp webp heic psd tiff", 130},
        {L"doc docx pdf ppt pptx xls xlsx txt md log csv odt rtf", 210},
        {L"db sqlite bak old dmp", 350},
        {L"jar class pyc wasm node", 190},
    };
    if (!ext.empty()) {
        for (const auto& g : groups) {
            const std::wstring list = g.exts;
            std::size_t pos = 0;
            for (;;) {
                const std::size_t space = list.find(L' ', pos);
                const std::wstring token =
                    list.substr(pos, space == std::wstring::npos ? std::wstring::npos : space - pos);
                if (token == ext)
                    return g.hue;
                if (space == std::wstring::npos)
                    break;
                pos = space + 1;
            }
        }
    }
    std::uint32_t hash = 2166136261u;
    for (const wchar_t c : ext)
        hash = (hash ^ static_cast<std::uint32_t>(c)) * 16777619u;
    return static_cast<int>(hash % 360);
}

inline COLORREF fileColor(const std::wstring& name, int depth, const Palette& p)
{
    const int hue = extHue(name);
    return p.dark?hsl(hue,25,25+depth*2):hsl(hue,42,89-depth*2);
}

inline RECT toRect(const Rect& r)
{
    return {static_cast<int>(std::lround(r.x)), static_cast<int>(std::lround(r.y)),
            static_cast<int>(std::lround(r.x + r.w)), static_cast<int>(std::lround(r.y + r.h))};
}

inline bool inRect(const RECT& r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct ViewerState {
    WindowIcons windowIcons;
    std::mutex m;
    std::condition_variable cv;
    std::thread worker; // the window thread
    HWND hwnd = nullptr;
    bool running = false;
    bool visible = false;
    std::string error;

    // ---- GUI-thread-only state ----
    ViewerSpec spec;
    Palette palette;
    int dpi = 96;
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr, fontHeading = nullptr;

    // The host's new look, waited for by the window thread (under `m`).
    winshun::Json pendingAppearance;

    std::shared_ptr<Node> tree; // the last completed scan (immutable once set)
    bool scanning = false;
    bool cancelled = false;
    std::atomic<bool> cancelFlag {false};
    std::thread scanThread;
    ScanProgress progress;
    std::string lastScanError; // UTF-8, shown in the content area

    std::vector<LargeFile> largest; // the current folder's top 1000
    std::vector<LargeFile> rootLargest; // cached scan-wide ranking
    std::vector<const Node*> folders; // immediate folders, including empty ones
    std::vector<SkippedDir> skippedList;
    std::uint64_t files = 0, dirs = 0, skippedDirs = 0, skippedLinks = 0;
    std::uint64_t elapsedMs = 0;
    std::wstring lastPath; // the display path of the last started scan

    bool sunburst = false;
    const Node* viewRoot = nullptr; // into *tree
    std::vector<const Node*> drill; // ancestors, excluding the scan root and viewRoot
    std::vector<Tile> tiles;
    std::vector<Slice> slices;

    int hoverTile = -1, hoverSlice = -1, hoverTop = -1, hoverCrumb = -1;
    int hoverDrive = -1; // welcome page: the card under the mouse
    bool hoverScan = false, hoverBack = false, hoverTreemapBtn = false, hoverSunburstBtn = false;

    RECT rcClient = {}, rcToolbar = {}, rcContent = {}, rcChart = {}, rcTopList = {}, rcStatus = {};
    RECT rcEdit = {}, rcScan = {}, rcBack = {}, rcTreemapBtn = {}, rcSunburstBtn = {};
    std::vector<RECT> crumbRects;
    std::vector<const Node*> crumbNodes;
    int topScroll = 0;

    // The right pane's mode: the largest files, or the folders of the level
    // on display. Single click selects; double click enters a folder.
    bool listFolders = true;
    int hoverFolder = -1;
    RECT rcListFiles = {}, rcListFolders = {}; // the two header tabs
    RECT rcSkipped = {}; // status-bar link to the unreadable directory details

    // Welcome page only: the drive cards currently on display, parallel to
    // their rects. Drive metadata is cached when the welcome page opens.
    std::vector<Drive> driveCards;
    std::vector<RECT> driveRects;

    HWND edit = nullptr;
    WNDPROC origEditProc = nullptr;
    HFONT editFont = nullptr;
    HBRUSH editBrush = nullptr;

    struct Row { const Node* node; std::wstring path; };
    std::vector<Row> rows;
    HWND filterEdit = nullptr;
    WNDPROC origFilterProc = nullptr;
    std::wstring filter, selectedPath, hoveredPath, notice, pendingPath;
    const Node* selected = nullptr;
    const Node* hovered = nullptr;
    std::vector<std::wstring> history;
    int historyIndex = -1;
    bool replayHistory = false, editingAddress = false, sortName = false, ascending = false;
    bool narrow = false, mobileList = false, dragging = false, tipVisible = false;
    bool partialView = false, resultIncomplete = false;
    bool suppressClick = false, fastResult = false;
    bool keyboardFocus = false;
    int focusIndex = -1;
    std::uint32_t unknownSizes=0, orphanRecords=0;
    int depth = 1;
    double splitRatio = .58;
    POINT pointer = {};
    ULONGLONG scanStarted = 0;
    std::shared_ptr<ScanResult> completeResult, partialResult;
    RECT rcHistBack={}, rcHistForward={}, rcAddressEdit={}, rcSource={}, rcProgress={}, rcPreview={};
    RECT rcOverview={}, rcChartPane={}, rcChartTools={}, rcDepth={}, rcDivider={}, rcDetail={};
    RECT rcEnter={}, rcLocate={}, rcFilter={}, rcSortName={}, rcSortSize={}, rcListFoot={};
    RECT rcPrevious={}, rcNext={}, rcShowChart={}, rcShowList={}, rcTheme={}, rcAccounting={}, rcDrive={};
    // Aggregation owns only minorItems synthetic nodes; data nodes remain in the immutable scan tree.
    std::deque<Node> groups;
    std::vector<std::pair<const Node*, std::vector<const Node*>>> groupMembers;
    std::vector<const Node*> groupFilter;

    // metrics, computed from dpi
    int toolbarH = 40, statusH = 22, topPaneW = 300, rowH = 22, topHeaderH = 28;
};

inline ViewerState& state()
{
    static ViewerState s;
    return s;
}

inline int S(int v, const ViewerState& s) { return MulDiv(v, s.dpi, 96); }

inline std::wstring joinPath(const std::wstring& base, const std::wstring& name)
{
    return childPath(base, name);
}

inline std::wstring nodePath(const ViewerState& s, const Node* n)
{
    if (!s.tree)
        return {};
    if (n == s.tree.get())
        return s.tree->name;
    std::wstring path = s.tree->name;
    for (const Node* d : s.drill)
        path = joinPath(path, d->name);
    return joinPath(path, n->name);
}

inline std::wstring fileNameOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Recompute only on navigation or scan completion, never on paint or resize.
// Filtering the scan-wide ranking would hide smaller files in a subtree.

inline std::wstring lower(std::wstring v) {
    for (auto& c : v) c = static_cast<wchar_t>(::towlower(c));
    return v;
}
inline void refreshRows(ViewerState& s) {
    s.rows.clear(); s.topScroll = 0;
    if (!s.viewRoot) return;
    const auto query = lower(s.filter);
    const auto add = [&](const Node* n, const std::wstring& path) {
        if (query.empty() || lower(path).find(query) != std::wstring::npos)
            s.rows.push_back({n, path});
    };
    if (s.listFolders) {
        const auto base = nodePath(s, s.viewRoot);
        for (const auto& n : s.viewRoot->children)
            if (s.groupFilter.empty() || std::find(s.groupFilter.begin(),s.groupFilter.end(),&n)!=s.groupFilter.end())
                add(&n, childPath(base,n.name));
    } else {
        for (const auto& f : s.largest) {
            const Node* n = s.viewRoot;
            const auto base = childPath(nodePath(s,s.viewRoot), L"");
            std::size_t pos = base.size();
            while(n && pos < f.path.size()) {
                auto end = f.path.find(L'\\',pos);
                n = findChild(*n,f.path.substr(pos,end==std::wstring::npos?end:end-pos));
                if(end==std::wstring::npos) break;
                pos=end+1;
            }
            if(n) add(n,f.path);
        }
    }
    std::stable_sort(s.rows.begin(),s.rows.end(),[&](const auto& a,const auto& b){
        if(s.sortName) {
            const auto order=::CompareStringOrdinal(a.node->name.c_str(),-1,b.node->name.c_str(),-1,TRUE);
            return s.ascending?order==CSTR_LESS_THAN:order==CSTR_GREATER_THAN;
        }
        return s.ascending ? a.node->logical<b.node->logical : a.node->logical>b.node->logical;
    });
}
inline void rebuildLists(ViewerState& s) {
    s.folders.clear(); s.largest.clear(); s.topScroll=0;
    s.hoverTop=s.hoverFolder=-1;
    if(!s.viewRoot) return;
    for(const Node& n:s.viewRoot->children) if(n.dir) s.folders.push_back(&n);
    if(s.viewRoot==s.tree.get() && !s.rootLargest.empty()) s.largest=s.rootLargest;
    else if(!s.listFolders) {
        Ctx ctx;
        const auto visit=[&](const auto& self,const Node& dir,const std::wstring& path)->void {
            for(const auto& n:dir.children) {
                if(n.dir) {
                    if(ctx.top.size()<kMaxLargest || n.logical>ctx.top.top().logical) self(self,n,childPath(path,n.name));
                } else considerLarge(ctx,n.logical,path,n.name.c_str());
            }
        };
        visit(visit,*s.viewRoot,nodePath(s,s.viewRoot));
        while(!ctx.top.empty()){s.largest.push_back(ctx.top.top());ctx.top.pop();}
        std::sort(s.largest.begin(),s.largest.end(),std::greater<>());
    }
    refreshRows(s);
}
inline int listCount(const ViewerState& s) { return static_cast<int>(s.rows.size()); }


// ---- fonts, layout -----------------------------------------------------------------

inline void ensureDpiAwareness()
{
    static const bool done = [] {
        if (const auto proc = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(reinterpret_cast<void*>(
                ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))))
            return proc(reinterpret_cast<HANDLE>(-4)) != FALSE; // PER_MONITOR_AWARE_V2
        return ::SetProcessDPIAware() != FALSE;
    }();
    (void)done;
}

inline HFONT makeOneFont(int pt, bool bold, int dpi, bool zh=false)
{
    const int px = -MulDiv(pt, dpi, 72);
    return ::CreateFontW(px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, zh?L"Microsoft YaHei UI":L"Segoe UI");
}

inline void applyEditFont(ViewerState& s)
{
    if (!s.edit)
        return;
    if (s.editFont)
        ::DeleteObject(s.editFont);
    s.editFont = makeOneFont(10, false, s.dpi, s.spec.zh);
    ::SendMessageW(s.edit, WM_SETFONT, reinterpret_cast<WPARAM>(s.editFont), TRUE);
}

inline void makeFonts(ViewerState& s)
{
    if (s.font)
        ::DeleteObject(s.font);
    if (s.fontBold)
        ::DeleteObject(s.fontBold);
    if (s.fontSmall)
        ::DeleteObject(s.fontSmall);
    s.font = makeOneFont(10, false, s.dpi, s.spec.zh);
    s.fontBold = makeOneFont(11, true, s.dpi, s.spec.zh);
    s.fontSmall = makeOneFont(9, false, s.dpi, s.spec.zh);
    if(s.fontHeading)::DeleteObject(s.fontHeading);
    s.fontHeading=makeOneFont(15,true,s.dpi,s.spec.zh);
    applyEditFont(s);
    if(s.filterEdit)::SendMessageW(s.filterEdit,WM_SETFONT,reinterpret_cast<WPARAM>(s.font),TRUE);
}


inline int maxRows(const ViewerState& s) {
    return std::max(0,static_cast<int>(s.rcTopList.bottom-s.rcTopList.top-s.topHeaderH-S(32,s))/s.rowH);
}
inline void computeLayout(ViewerState& s) {
    ::GetClientRect(s.hwnd,&s.rcClient);
    const int w=s.rcClient.right,h=s.rcClient.bottom,m=S(16,s),gap=S(6,s),bh=S(32,s);
    const bool compact=w<S(560,s);
    s.narrow=w<S(760,s); s.rowH=S(37,s); s.topHeaderH=S(122,s);s.statusH=S(26,s);
    const int nav=S(52,s), addressY=compact?nav+bh+gap:nav;
    s.rcHistBack={m,nav,m+bh,nav+bh};
    s.rcHistForward={m+bh+gap,nav,m+2*bh+gap,nav+bh};
    s.rcBack={m+2*(bh+gap),nav,m+3*bh+2*gap,nav+bh};
    s.rcScan={w-m-S(100,s),nav,w-m,nav+bh};
    s.rcTheme={w-m-S(60,s),S(8,s),w-m,S(38,s)};
    s.rcEdit={compact?m:s.rcBack.right+gap,addressY,compact?w-m:s.rcScan.left-gap,addressY+bh};
    s.rcAddressEdit={s.rcEdit.right-S(66,s),addressY,s.rcEdit.right,addressY+bh};
    s.rcSource={m,s.rcEdit.bottom+S(10,s),w-m,s.rcEdit.bottom+S(40,s)};
    s.rcDrive={m,s.rcSource.top,m+S(100,s),s.rcSource.bottom};
    s.rcAccounting={s.rcDrive.right+S(8,s),s.rcSource.top,w-m-S(110,s),s.rcSource.bottom};
    s.rcSkipped={w-m-S(110,s),s.rcSource.top,w-m,s.rcSource.bottom};
    int y=s.rcSource.bottom+S(10,s);
    s.rcProgress={};s.rcPreview={};
    if(s.scanning) {
        s.rcProgress={m,y,w-m,y+S(compact?110:92,s)};
        s.rcPreview={w-m-S(142,s),y+S(10,s),w-m-S(10,s),y+S(38,s)};
        y=s.rcProgress.bottom+S(10,s);
    }
    if(!s.notice.empty()) y+=S(28,s);
    s.rcOverview={m,y,w-m,y+S(38,s)}; y=s.rcOverview.bottom;
    s.rcShowChart=s.rcShowList={};
    if(s.narrow && s.tree) {
        s.rcShowChart={m,y,m+S(80,s),y+bh};s.rcShowList={m+S(84,s),y,m+S(164,s),y+bh};y+=bh+S(8,s);
    }
    s.toolbarH=y;s.rcToolbar={0,0,w,y};
    s.rcStatus={m,h-s.statusH,w-m,h};
    const int detailHeight=S(s.narrow?146:108,s);
    s.rcDetail={m,h-s.statusH-detailHeight,w-m,h-s.statusH-S(8,s)};
    const int bottom=s.tree?s.rcDetail.top-S(12,s):h-s.statusH-S(10,s);
    s.rcContent={m,y,w-m,std::max(y+S(80,s),bottom)};
    s.rcTopList={};s.rcDivider={};s.rcChartPane=s.rcContent;
    if(s.tree) {
        if(s.narrow) {
            if(s.mobileList) {s.rcTopList=s.rcContent;s.rcChartPane={};}
        } else {
            const int divider=m+static_cast<int>((w-2*m)*s.splitRatio);
            s.rcChartPane.right=divider;
            s.rcDivider={divider,y,divider+S(7,s),s.rcContent.bottom};
            s.rcTopList={s.rcDivider.right,y,w-m,s.rcContent.bottom};
        }
    }
    s.rcChart=s.rcChartPane;
    s.rcChart.top+=S(94,s);s.rcChart.bottom-=S(32,s);
    s.rcChart.left+=S(10,s);s.rcChart.right-=S(10,s);
    s.rcTreemapBtn=s.rcSunburstBtn=s.rcDepth={};
    if(s.rcChartPane.right>s.rcChartPane.left) {
        const int right=s.rcChartPane.right-S(12,s);
        s.rcSunburstBtn={right-S(54,s),y+S(9,s),right,y+S(38,s)};
        s.rcTreemapBtn={right-S(112,s),y+S(9,s),right-S(58,s),y+S(38,s)};
        s.rcDepth={s.rcChartPane.left+S(12,s),y+S(55,s),s.rcChartPane.left+S(122,s),y+S(82,s)};
    }
    s.rcListFiles=s.rcListFolders=s.rcFilter=s.rcSortName=s.rcSortSize=s.rcListFoot=s.rcPrevious=s.rcNext={};
    if(s.rcTopList.right>s.rcTopList.left) {
        const int left=s.rcTopList.left+S(12,s),right=s.rcTopList.right-S(12,s);
        s.rcListFolders={left,y+S(9,s),left+S(70,s),y+S(38,s)};
        s.rcListFiles={left+S(74,s),y+S(9,s),left+S(158,s),y+S(38,s)};
        s.rcFilter={left,y+S(58,s),right,y+S(86,s)};
        s.rcSortName={left,y+S(96,s),right-S(140,s),y+S(122,s)};
        s.rcSortSize={right-S(140,s),y+S(96,s),right,y+S(122,s)};
        s.rcListFoot={left,s.rcTopList.bottom-S(30,s),right,s.rcTopList.bottom};
        s.rcNext={right-S(54,s),s.rcListFoot.top,right,s.rcListFoot.bottom};
        s.rcPrevious={right-S(112,s),s.rcListFoot.top,right-S(58,s),s.rcListFoot.bottom};
    }
    const int actionY=s.narrow?s.rcDetail.bottom-S(38,s):s.rcDetail.top+S(12,s);
    const int actionX=s.narrow?s.rcDetail.left+S(12,s):s.rcDetail.right-S(232,s);
    s.rcEnter={actionX,actionY,actionX+S(100,s),actionY+bh};
    s.rcLocate={actionX+S(106,s),actionY,actionX+S(220,s),actionY+bh};
    if(s.edit) {
        ::MoveWindow(s.edit,s.rcEdit.left+1,s.rcEdit.top+1,s.rcEdit.right-s.rcEdit.left-2,bh-2,TRUE);
        ::ShowWindow(s.edit,(!s.tree||s.editingAddress)?SW_SHOW:SW_HIDE);
    }
    if(s.filterEdit) {
        ::MoveWindow(s.filterEdit,s.rcFilter.left+1,s.rcFilter.top+1,std::max(0L,s.rcFilter.right-s.rcFilter.left-2),S(28,s)-2,TRUE);
        ::ShowWindow(s.filterEdit,s.rcFilter.right>s.rcFilter.left?SW_SHOW:SW_HIDE);
    }
}
struct FocusTarget { RECT rect; HWND child = nullptr; bool list = false; };
inline std::vector<FocusTarget> focusTargets(const ViewerState& s) {
    std::vector<FocusTarget> targets;
    const auto add=[&](const RECT& r,bool enabled=true,HWND child=nullptr,bool list=false) {
        if(enabled&&r.right>r.left&&r.bottom>r.top)targets.push_back({r,child,list});
    };
    add(s.rcHistBack,s.historyIndex>0);
    add(s.rcHistForward,s.historyIndex+1<int(s.history.size()));
    add(s.rcBack,s.viewRoot&&s.viewRoot!=s.tree.get());
    if(!s.tree||s.editingAddress)add(s.rcEdit,true,s.edit);
    else {
        for(const auto& crumb:s.crumbRects)add(crumb);
        add(s.rcAddressEdit);
    }
    add(s.rcScan);add(s.rcDrive);add(s.rcAccounting);add(s.rcSkipped);add(s.rcTheme);
    add(s.rcPreview,s.scanning);add(s.rcShowChart);add(s.rcShowList);
    add(s.rcTreemapBtn,s.tree!=nullptr);add(s.rcSunburstBtn,s.tree!=nullptr);add(s.rcDepth,s.tree!=nullptr);
    add(s.rcListFolders);add(s.rcListFiles);add(s.rcFilter,true,s.filterEdit);
    add(s.rcSortName);add(s.rcSortSize);
    RECT rows=s.rcTopList;rows.top+=s.topHeaderH;rows.bottom=s.rcListFoot.top;
    add(rows,!s.rows.empty(),nullptr,true);
    add(s.rcPrevious,s.topScroll>0);add(s.rcNext,s.topScroll+maxRows(s)<listCount(s));
    add(s.rcEnter,s.selected&&s.selected->dir);add(s.rcLocate,s.selected!=nullptr);
    for(const auto& drive:s.driveRects)add(drive);
    return targets;
}
inline void cycleFocus(ViewerState& s,int direction) {
    const auto targets=focusTargets(s);
    if(targets.empty())return;
    int index=s.keyboardFocus?s.focusIndex:-1;
    const auto focused=::GetFocus();
    for(int i=0;i<int(targets.size());++i)if(targets[i].child&&targets[i].child==focused)index=i;
    if(index<0)index=direction>0?-1:0;
    s.focusIndex=(index+direction+int(targets.size()))%int(targets.size());
    s.keyboardFocus=true;
    const auto& target=targets[s.focusIndex];
    ::SetFocus(target.child?target.child:s.hwnd);
    ::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline void drawKeyboardFocus(const ViewerState& s,HDC dc) {
    if(!s.keyboardFocus||::GetFocus()!=s.hwnd)return;
    const auto targets=focusTargets(s);
    if(s.focusIndex<0||s.focusIndex>=int(targets.size())||targets[s.focusIndex].child)return;
    RECT rect=targets[s.focusIndex].rect;
    ::InflateRect(&rect,-S(3,s),-S(3,s));
    ::DrawFocusRect(dc,&rect);
}

inline std::vector<const Node*> visibleChildren(ViewerState& s,const Node& parent,double area) {
    std::vector<const Node*> shown,minorItems;
    for(const auto& n:parent.children) {
        if(!n.logical) continue;
        if(parent.logical && area*static_cast<double>(n.logical)/parent.logical<S(9,s)*S(9,s)) minorItems.push_back(&n);
        else shown.push_back(&n);
    }
    if(!minorItems.empty()) {
        s.groups.emplace_back();auto& g=s.groups.back();g.name=(s.spec.zh?L"其他 ":L"Other ")+std::to_wstring(minorItems.size())+(s.spec.zh?L" 项":L" items");
        for(auto n:minorItems){g.logical+=n->logical;g.files+=n->dir?n->files:1;}
        shown.push_back(&g);s.groupMembers.push_back({&g,std::move(minorItems)});
    }
    return shown;
}
inline void relayoutView(ViewerState& s) {
    s.tiles.clear();s.slices.clear();s.groups.clear();s.groupMembers.clear();
    s.hoverTile=s.hoverSlice=s.hoverTop=s.hoverCrumb=s.hoverFolder=-1;s.hovered=nullptr;s.hoveredPath.clear();s.tipVisible=false;
    s.crumbRects.clear();s.crumbNodes.clear();
    if(!s.tree||!s.viewRoot||s.rcChart.right<=s.rcChart.left||s.rcChart.bottom<=s.rcChart.top) return;
    const auto base=nodePath(s,s.viewRoot);
    Rect area{double(s.rcChart.left),double(s.rcChart.top),double(s.rcChart.right-s.rcChart.left),double(s.rcChart.bottom-s.rcChart.top)};
    auto kids=visibleChildren(s,*s.viewRoot,area.area());
    if(!s.sunburst) {
        treemapTiles(*s.viewRoot,area,0,0,s.tiles,base,0,&kids);
        if(s.depth==2) {
            const auto parents=s.tiles;
            s.tiles.clear();
            for(const auto& t:parents) {
                s.tiles.push_back(t);
                if(!t.node->dir||t.rect.h<S(70,s)||t.rect.w<S(55,s)) continue;
                Rect inside{t.rect.x+2,t.rect.y+S(27,s),t.rect.w-4,t.rect.h-S(29,s)};
                auto sub=visibleChildren(s,*t.node,inside.area());
                treemapTiles(*t.node,inside,1,0,s.tiles,t.path,1,&sub);
            }
        }
    } else {
        const double radius=std::min(area.w,area.h)/2-S(8,s),hole=std::max(12.0,radius*.44);
        if(radius<=hole) return;
        const double ring=(radius-hole)/s.depth;
        sunburstSlices(*s.viewRoot,0,0,hole,ring,-3.141592653589793/2,3*3.141592653589793/2,0,0,s.slices,base,0,&kids);
        if(s.depth==2) {
            const auto parents=s.slices;
            for(const auto& sl:parents) if(sl.node->dir) {
                auto sub=visibleChildren(s,*sl.node,(sl.a1-sl.a0)*radius*ring);
                sunburstSlices(*sl.node,0,0,hole+ring,ring,sl.a0,sl.a1,1,0,s.slices,sl.path,1,&sub);
            }
        }
    }
}


// ---- scanning ----------------------------------------------------------------------

inline void joinScanThread(ViewerState& s)
{
    if (s.scanThread.joinable())
        s.scanThread.join();
}


inline void scanThreadMain(HWND hwnd,std::wstring path,bool fast) {
    ViewerState& s=state();auto* result=new ScanResult();
    try {
        ULONGLONG lastPost=0;
        std::wstring fallback;
        const ProgressFn progress=[&](const ScanProgress& p) {
            if(s.cancelFlag)return false;
            const auto now=::GetTickCount64();if(now-lastPost<100)return true;lastPost=now;
            auto* copy=new ScanProgress(p);copy->fallbackReason=fallback;if(!::PostMessageW(hwnd,WM_APP_PROGRESS,0,reinterpret_cast<LPARAM>(copy)))delete copy;return true;
        };
        const SnapshotFn snapshot=[hwnd](std::shared_ptr<ScanResult> part) {
            auto* packet=new std::shared_ptr<ScanResult>(std::move(part));
            if(!::PostMessageW(hwnd,WM_APP_PARTIAL,0,reinterpret_cast<LPARAM>(packet)))delete packet;
        };
        bool ordinary=true;
        if(fast) {
            std::wstring reason;
            const auto outcome=helper::scan(hwnd,path,progress,snapshot,s.cancelFlag,*result,reason);
            ordinary=outcome==helper::Outcome::Fallback;
            if(outcome==helper::Outcome::Cancelled)result->cancelled=true;
            if(ordinary){fallback=reason;ScanProgress pg;pg.current=path;lastPost=0;progress(pg);}
        }
        if(ordinary&&!s.cancelFlag)scanPath(path,progress,s.cancelFlag,result,snapshot);
        else if(s.cancelFlag)result->cancelled=true;
        result->fallbackReason=fallback;
    } catch(const std::exception& e){result->error=std::string("Scan failed: ")+e.what();}
    if(!::PostMessageW(hwnd,WM_APP_DONE,0,reinterpret_cast<LPARAM>(result)))delete result;
}

inline void startScan(ViewerState& s, const std::wstring& rawPath)
{
    if (s.scanning)
        return;
    std::wstring prefixed;
    const std::wstring display = normalizePath(rawPath, &prefixed);
    if (display.empty()) {
        s.lastScanError = s.spec.zh ? "路径为空，请先输入一个要扫描的路径。"
                                    : "The path is empty; type a path to scan first.";
        s.driveRects.clear();
        computeLayout(s);
        relayoutView(s);
        ::InvalidateRect(s.hwnd, nullptr, FALSE);
        return;
    }

    bool fast=false;
    if(mft::eligible(display)) {
        const int choice=::MessageBoxW(s.hwnd,s.spec.zh?
            L"快速扫描只读取 NTFS 文件记录，不读取文件内容。\r\n仅独立扫描助手按需申请管理员权限，主界面保持普通权限。\r\n\r\n是：授权快速扫描\r\n否：普通扫描\r\n取消：返回\r\n拒绝 Windows 授权或助手失败时将自动使用普通扫描。":
            L"Fast scanning reads NTFS metadata only. Only the short-lived helper requests administrator access; this window stays unelevated.\r\n\r\nYes: fast scan\r\nNo: standard scan\r\nCancel: return\r\nDeclining Windows authorization or a helper failure falls back to standard scanning.",
            s.spec.zh?L"选择扫描方式":L"Choose scan mode",MB_YESNOCANCEL|MB_ICONINFORMATION);
        if(choice==IDCANCEL)return;fast=choice==IDYES;
    }
    s.scanning = true;
    s.scanStarted=::GetTickCount64();s.partialResult.reset();s.partialView=!s.completeResult;
    s.notice.clear();s.pendingPath.clear();s.editingAddress=false;
    ::SetTimer(s.hwnd,1,100,nullptr);
    s.cancelled = false;
    s.lastScanError.clear();
    s.cancelFlag = false;
    s.progress = ScanProgress {};
    s.lastPath = display;
    if(!s.tree)::SetWindowTextW(s.edit, display.c_str());
    s.driveCards.clear();
    s.driveRects.clear();
    s.hoverDrive = -1;
    computeLayout(s);
    relayoutView(s);
    joinScanThread(s);
    s.scanThread = std::thread(scanThreadMain, s.hwnd, display,fast);
    ::InvalidateRect(s.hwnd, nullptr, FALSE);
}

inline void saveConfigNow(const ViewerState& s)
{
    ViewConfig c;
    c.lastPath = s.lastPath;
    c.hasFile = true;
    saveViewConfig(s.spec.dataDir, c);
}

// ---- painting ----------------------------------------------------------------------

inline void fillWith(HDC hdc, const RECT& r, COLORREF c)
{
    HBRUSH b = ::CreateSolidBrush(c);
    RECT rr = r;
    ::FillRect(hdc, &rr, b);
    ::DeleteObject(b);
}

inline void frameWith(HDC hdc, const RECT& r, COLORREF c)
{
    HBRUSH b = ::CreateSolidBrush(c);
    RECT rr = r;
    ::FrameRect(hdc, &rr, b);
    ::DeleteObject(b);
}

inline void drawTextIn(HDC hdc, const std::wstring& text, RECT r, COLORREF color, UINT flags)
{
    ::SetTextColor(hdc, color);
    ::DrawTextW(hdc, text.c_str(), -1, &r, flags | DT_NOPREFIX | DT_SINGLELINE);
}

inline void drawButton(ViewerState& s, HDC hdc, const RECT& r, const std::wstring& label, bool active,
                       bool hover)
{
    const bool disabled=(&r==&s.rcHistBack&&s.historyIndex<=0)
        ||(&r==&s.rcHistForward&&s.historyIndex+1>=int(s.history.size()))
        ||(&r==&s.rcBack&&(!s.viewRoot||s.viewRoot==s.tree.get()))
        ||(&r==&s.rcEnter&&(!s.selected||!s.selected->dir))
        ||(&r==&s.rcLocate&&!s.selected)
        ||(&r==&s.rcPrevious&&s.topScroll==0)
        ||(&r==&s.rcNext&&s.topScroll+maxRows(s)>=listCount(s));
    hover=hover||(!disabled&&inRect(r,s.pointer.x,s.pointer.y));
    const bool primary=active&&(&r==&s.rcScan||&r==&s.rcEnter);
    const auto fill=primary?s.palette.accentFill:hover||active?s.palette.hover:s.palette.panel;
    const auto brush=::CreateSolidBrush(fill);const auto pen=::CreatePen(PS_SOLID,1,active?s.palette.accent:s.palette.border);
    const auto oldBrush=::SelectObject(hdc,brush),oldPen=::SelectObject(hdc,pen);
    ::RoundRect(hdc,r.left,r.top,r.right,r.bottom,S(8,s),S(8,s));
    ::SelectObject(hdc,oldBrush);::SelectObject(hdc,oldPen);::DeleteObject(brush);::DeleteObject(pen);
    const COLORREF text = disabled?s.palette.muted:primary?s.palette.accentText:active?s.palette.accent:s.palette.text;
    RECT tr = r;
    ::OffsetRect(&tr, 0, 1);
    drawTextIn(hdc, label, tr, text, DT_CENTER | DT_VCENTER);
}

// The hover status line for one node: kind, size, counts, unreadable entries.
inline bool sizeUnknown(const Node& n) { return n.skippedChildren && !n.logical; }
inline std::wstring sizeLabel(const ViewerState& s, const Node& n) {
    if (sizeUnknown(n)) return s.spec.zh ? L"大小未知" : L"Size unknown";
    return winshun::wide(formatBytes(n.logical, s.spec.zh));
}
inline std::wstring describe(const ViewerState& s, const Node* n)
{
    const bool zh = s.spec.zh;
    std::wstring t = n->dir ? (zh ? L"文件夹 · " : L"folder · ") : (zh ? L"文件 · " : L"file · ");
    t += sizeLabel(s, *n);
    // The share of the node inside its parent, when it is a child in the
    // displayed tree (the view root itself has no parent there).
    if (s.viewRoot && n != s.viewRoot && s.viewRoot->logical > 0 && n->logical > 0)
        t += (zh ? L" · 占本层 " : L" · ") + std::to_wstring(n->logical * 100 / s.viewRoot->logical)
             + L"%";
    if (n->dir) {
        t += (zh ? L" · " : L" · ") + winshun::wide(formatCount(n->files)) + (zh ? L" 个文件" : L" files");
        t += (zh ? L" · " : L" · ") + winshun::wide(formatCount(n->dirs)) + (zh ? L" 个文件夹" : L" folders");
        if (n->skippedChildren > 0)
            t += (zh ? L" · " : L" · ") + std::to_wstring(n->skippedChildren)
                 + (zh ? L" 项无法读取" : L" unreadable");
    } else if (n->allocApprox > n->logical) {
        t += (zh ? L"（按簇约 " : L" (~") + winshun::wide(formatBytes(n->allocApprox, zh)) + (zh ? L"）" : L")");
    }
    return t;
}

inline void drawTiles(ViewerState& s, HDC hdc)
{
    for (std::size_t i = 0; i < s.tiles.size(); ++i) {
        const Tile& t = s.tiles[i];
        const RECT r = toRect(t.rect);
        if (r.right <= r.left || r.bottom <= r.top)
            continue;
        COLORREF fill =
            t.node->dir ? dirColor(t.depth, s.palette) : fileColor(t.node->name, t.depth, s.palette);
        if (t.node == s.hovered || t.node == s.selected)
            fill = lighten(fill, s.palette.dark ? 18 : -14);
        fillWith(hdc, r, fill);
        frameWith(hdc, r, s.palette.bg);
        RECT rr = r;
        ::InflateRect(&rr, -S(4, s), -S(2, s));
        const int w = rr.right - rr.left, h = rr.bottom - rr.top;
        if (w >= S(48, s) && h >= S(15, s)) {
            const COLORREF text = tileText(fill);
            drawTextIn(hdc, t.node->name, rr, text, DT_LEFT | DT_END_ELLIPSIS);
            if (h >= S(40, s) && !(t.node->dir && s.depth==2 && t.depth==0)) {
                RECT sr = rr;
                sr.top += S(14, s);
                const COLORREF sub = s.palette.dark ? lighten(text, -70) : lighten(text, 70);
                drawTextIn(hdc, winshun::wide(formatBytes(t.node->logical, s.spec.zh)), sr, sub,
                           DT_LEFT | DT_END_ELLIPSIS);
            }
        }
        if (t.node == s.hovered || t.node == s.selected) {
            frameWith(hdc, r, s.palette.accent);
            RECT r2 = r;
            ::InflateRect(&r2, -1, -1);
            frameWith(hdc, r2, s.palette.accent);
        }
    }
}

inline void drawSlices(ViewerState& s, HDC hdc)
{
    if (s.slices.empty())
        return;
    const double cx = (s.rcChart.left + s.rcChart.right) / 2.0;
    const double cy = (s.rcChart.top + s.rcChart.bottom) / 2.0;
    HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(DC_BRUSH));
    HGDIOBJ oldPen = ::SelectObject(hdc, ::GetStockObject(DC_PEN));
    for (std::size_t i = 0; i < s.slices.size(); ++i) {
        const Slice& sl = s.slices[i];
        COLORREF fill =
            sl.dir ? dirColor(sl.depth, s.palette) : fileColor(sl.node->name, sl.depth, s.palette);
        if (sl.node == s.hovered || sl.node == s.selected)
            fill = lighten(fill, s.palette.dark ? 22 : -18);
        ::SetDCBrushColor(hdc, fill);
        ::SetDCPenColor(hdc, sl.node==s.selected||sl.node==s.hovered?s.palette.accent:s.palette.panel);
        const int steps = std::max(2, static_cast<int>(std::ceil((sl.a1 - sl.a0) / 0.03)));
        std::vector<POINT> pts;
        pts.reserve(static_cast<std::size_t>(steps) * 2 + 2);
        for (int k = 0; k <= steps; ++k) {
            const double a = sl.a0 + (sl.a1 - sl.a0) * k / steps;
            pts.push_back({static_cast<LONG>(std::lround(cx + std::cos(a) * sl.r1)),
                           static_cast<LONG>(std::lround(cy + std::sin(a) * sl.r1))});
        }
        for (int k = steps; k >= 0; --k) {
            const double a = sl.a0 + (sl.a1 - sl.a0) * k / steps;
            pts.push_back({static_cast<LONG>(std::lround(cx + std::cos(a) * sl.r0)),
                           static_cast<LONG>(std::lround(cy + std::sin(a) * sl.r0))});
        }
        ::Polygon(hdc, pts.data(), static_cast<int>(pts.size()));
    }
    ::SelectObject(hdc, oldBrush);
    ::SelectObject(hdc, oldPen);

    // The center hole names what is on display and its total: the name above
    // the center line, the total below.
    const std::wstring name = s.viewRoot ? s.viewRoot->name : std::wstring();
    const std::wstring total = winshun::wide(formatBytes(s.viewRoot ? s.viewRoot->logical : 0, s.spec.zh));
    if(s.slices.front().r0<S(32,s))return;
    const auto inner=std::min<double>(s.slices.front().r0*.8,S(80,s));
    const RECT hole {static_cast<LONG>(cx - inner), static_cast<LONG>(cy - S(26, s)),
                     static_cast<LONG>(cx + inner), static_cast<LONG>(cy + S(26, s))};
    RECT nr = hole;
    nr.bottom = static_cast<LONG>(cy);
    RECT tr = hole;
    tr.top = static_cast<LONG>(cy);
    HGDIOBJ oldFont = ::SelectObject(hdc, s.fontBold);
    drawTextIn(hdc, name, nr, s.palette.text, DT_CENTER | DT_BOTTOM | DT_END_ELLIPSIS);
    ::SelectObject(hdc, oldFont);
    drawTextIn(hdc, total, tr, s.palette.muted, DT_CENTER | DT_TOP | DT_END_ELLIPSIS);
}


inline std::wstring share(const ViewerState& s,std::uint64_t bytes) {
    wchar_t b[40];std::swprintf(b,40,L"%.1f%%",s.viewRoot&&s.viewRoot->logical?100.0*bytes/s.viewRoot->logical:0.0);return b;
}
inline void drawTopList(ViewerState& s,HDC dc) {
    fillWith(dc,s.rcTopList,s.palette.panel);frameWith(dc,s.rcTopList,s.palette.border);
    fillWith(dc,s.rcFilter,s.palette.input);frameWith(dc,s.rcFilter,s.palette.border);
    drawButton(s,dc,s.rcListFolders,s.spec.zh?L"当前层":L"Current",s.listFolders,false);
    drawButton(s,dc,s.rcListFiles,s.spec.zh?L"最大文件":L"Largest",!s.listFolders,false);
    drawTextIn(dc,(s.spec.zh?L"名称":L"Name")+std::wstring(s.sortName?(s.ascending?L" ↑":L" ↓"):L""),s.rcSortName,s.palette.muted,DT_VCENTER);
    drawTextIn(dc,(s.spec.zh?L"大小":L"Size")+std::wstring(!s.sortName?(s.ascending?L" ↑":L" ↓"):L"")+L"          %",s.rcSortSize,s.palette.muted,DT_RIGHT|DT_VCENTER);
    const int count=listCount(s),rows=maxRows(s);s.topScroll=std::clamp(s.topScroll,0,std::max(0,count-rows));
    for(int i=0;i<rows&&i+s.topScroll<count;++i) {
        const auto& item=s.rows[i+s.topScroll];const auto* n=item.node;
        RECT row{s.rcTopList.left+1,s.rcTopList.top+s.topHeaderH+i*s.rowH,s.rcTopList.right-1,s.rcTopList.top+s.topHeaderH+(i+1)*s.rowH};
        if(s.selected==n) fillWith(dc,row,s.palette.hover);
        else if(s.hovered==n) fillWith(dc,row,s.palette.bg);
        fillWith(dc,{row.left,row.bottom-1,row.right,row.bottom},s.palette.border);
        RECT sw{row.left+S(12,s),row.top+S(15,s),row.left+S(21,s),row.top+S(24,s)};
        fillWith(dc,sw,n->dir?dirColor(0,s.palette):fileColor(n->name,0,s.palette));
        RECT name{sw.right+S(8,s),row.top,row.right-S(155,s),row.bottom};
        drawTextIn(dc,(n->dir?L"› ":L"")+n->name,name,s.selected==n?s.palette.accent:s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
        RECT size{row.right-S(153,s),row.top,row.right-S(68,s),row.bottom};
        drawTextIn(dc,n->skippedChildren&&n->logical==0?L"—":winshun::wide(formatBytes(n->logical,s.spec.zh)),size,s.palette.text,DT_RIGHT|DT_VCENTER);
        RECT percent{size.right+S(5,s),row.top,row.right-S(10,s),row.bottom};
        drawTextIn(dc,n->skippedChildren&&n->logical==0?L"—":share(s,n->logical),percent,s.palette.muted,DT_RIGHT|DT_VCENTER);
    }
    if(!count) {RECT r=s.rcTopList;r.top+=s.topHeaderH+S(12,s);drawTextIn(dc,s.spec.zh?L"没有匹配的条目":L"No matching items",r,s.palette.muted,DT_CENTER);}
    RECT foot=s.rcListFoot;foot.right=s.rcPrevious.left-S(6,s);
    drawTextIn(dc,std::to_wstring(count)+(s.spec.zh?L" 项 · 双击目录进入":L" items · double-click to enter"),foot,s.palette.muted,DT_VCENTER|DT_END_ELLIPSIS);
    drawButton(s,dc,s.rcPrevious,s.spec.zh?L"上一页":L"Prev",false,false);
    drawButton(s,dc,s.rcNext,s.spec.zh?L"下一页":L"Next",false,false);
}
inline void drawBreadcrumb(ViewerState& s,HDC dc) {
    s.crumbNodes.clear();s.crumbRects.clear();
    if(!s.tree||s.editingAddress) return;
    fillWith(dc,s.rcEdit,s.palette.panel);frameWith(dc,s.rcEdit,s.palette.border);
    std::vector<const Node*> chain{s.tree.get()};for(auto n:s.drill)chain.push_back(n);
    if(s.viewRoot!=s.tree.get())chain.push_back(s.viewRoot);
    const int available=s.rcAddressEdit.left-s.rcEdit.left-S(10,s);
    auto needed=[&](const std::vector<const Node*>& nodes){int total=0;for(auto n:nodes){SIZE z{};::GetTextExtentPoint32W(dc,n->name.c_str(),int(n->name.size()),&z);total+=std::min<int>(z.cx+S(22,s),S(150,s));}return total;};
    bool collapsed=false;
    while(chain.size()>3 && needed(chain)>available) {chain.erase(chain.begin()+1);collapsed=true;}
    int x=s.rcEdit.left+S(5,s);
    for(std::size_t i=0;i<chain.size();++i) {
        const auto n=chain[i];const std::wstring label=(collapsed&&i==1?L"… › ":i>0?L"› ":L"")+n->name;
        SIZE z{};::GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&z);
        const int reserve=int(chain.size()-i-1)*S(55,s);
        const int width=std::min<int>({z.cx+S(22,s),S(190,s),std::max<int>(S(25,s),s.rcAddressEdit.left-x-reserve)});
        RECT r{x,s.rcEdit.top,x+width,s.rcEdit.bottom};
        drawTextIn(dc,label,r,n==s.viewRoot?s.palette.accent:s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
        s.crumbNodes.push_back(n);s.crumbRects.push_back(r);x+=width;
    }
    drawTextIn(dc,s.spec.zh?L"编辑路径":L"Edit path",s.rcAddressEdit,s.palette.muted,DT_CENTER|DT_VCENTER);
}
inline void drawStatusRows(ViewerState& s,HDC dc) {
    if(s.tree&&s.viewRoot) {
        fillWith(dc,s.rcDetail,s.palette.panel);frameWith(dc,s.rcDetail,s.palette.border);
        const Node* n=s.selected?s.selected:s.viewRoot;
        const auto path=s.selected?s.selectedPath:nodePath(s,s.viewRoot);
        RECT line=s.rcDetail;::InflateRect(&line,-S(12,s),-S(8,s));
        if(!s.narrow)line.right=s.rcEnter.left-S(12,s);
        line.bottom=line.top+S(24,s);
        drawTextIn(dc,(s.selected?(s.spec.zh?L"已选中 · ":L"Selected · "):(s.spec.zh?L"当前目录 · ":L"Current · "))+n->name,line,s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
        line.top+=S(26,s);line.bottom+=S(26,s);line.right=s.rcDetail.right-S(12,s);
        drawTextIn(dc,path,line,s.palette.muted,DT_VCENTER|DT_PATH_ELLIPSIS);
        line.top+=S(25,s);line.bottom+=S(25,s);
        drawTextIn(dc,sizeLabel(s,*n)+(sizeUnknown(*n)?L"":(s.spec.zh?L" 逻辑大小 · ":L" logical · ")+share(s,n->logical))+L" · "+winshun::wide(formatCount(n->dir?n->files:1))+(s.spec.zh?L" 个文件":L" files"),line,s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
        drawButton(s,dc,s.rcEnter,s.spec.zh?L"进入文件夹":L"Enter folder",s.selected&&s.selected->dir,false);
        drawButton(s,dc,s.rcLocate,s.spec.zh?L"定位到所在目录":L"Locate parent",false,false);
    }
    std::wstring status=s.hovered?s.hoveredPath+(s.spec.zh?L" · ":L" · ")+describe(s,s.hovered):s.selected?s.selectedPath:(s.spec.zh?L"悬停查看完整路径；单击后详情保持":L"Hover for path; click to keep details");
    RECT r=s.rcStatus;
    if(!s.narrow)r.right-=S(180,s);
    drawTextIn(dc,status,r,s.palette.muted,DT_VCENTER|DT_END_ELLIPSIS);
    if(!s.narrow){r.left=r.right;r.right=s.rcStatus.right;drawTextIn(dc,L"Alt+←  /  Alt+→    Ctrl+L",r,s.palette.muted,DT_RIGHT|DT_VCENTER);}
}
inline void drawProgressBox(ViewerState& s,HDC dc) {
    if(!s.scanning)return;
    fillWith(dc,s.rcProgress,s.palette.hover);frameWith(dc,s.rcProgress,s.palette.border);
    RECT r=s.rcProgress;::InflateRect(&r,-S(12,s),-S(8,s));r.right=s.rcPreview.left-S(8,s);r.bottom=r.top+S(26,s);
    drawTextIn(dc,s.progress.fast?(s.spec.zh?L"NTFS 快速扫描 · 读取 MFT":L"NTFS fast scan · reading MFT"):(s.spec.zh?L"普通扫描 · 正在枚举目录":L"Standard scan · enumerating"),r,s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
    drawButton(s,dc,s.rcPreview,s.partialView?(s.spec.zh?L"查看上次完成结果":L"Previous result"):(s.spec.zh?L"查看本轮已发现":L"View partial result"),false,false);
    r.top+=S(30,s);r.bottom+=S(30,s);r.right=s.rcProgress.right-S(12,s);
    drawTextIn(dc,winshun::wide(formatCount(s.progress.files))+(s.spec.zh?L" 个文件 · ":L" files · ")+winshun::wide(formatBytes(s.progress.bytes,s.spec.zh))+L" · "+std::to_wstring((::GetTickCount64()-s.scanStarted)/1000)+L" s",r,s.palette.muted,DT_VCENTER|DT_END_ELLIPSIS);
    r.top+=S(24,s);r.bottom+=S(24,s);
    drawTextIn(dc,s.progress.current,r,s.palette.muted,DT_VCENTER|DT_PATH_ELLIPSIS);
    RECT track{s.rcProgress.left+S(12,s),s.rcProgress.bottom-S(5,s),s.rcProgress.right-S(12,s),s.rcProgress.bottom-S(2,s)};
    fillWith(dc,track,s.palette.border);const int span=(track.right-track.left)/5;
    track.left+=int((::GetTickCount64()/10)%800)*(track.right-track.left-span)/800;track.right=track.left+span;fillWith(dc,track,s.palette.accent);
}
inline void drawTooltip(ViewerState& s,HDC dc) {
    if(!s.tipVisible||!s.hovered)return;
    const int width=std::min<int>(S(340,s),s.rcClient.right-S(20,s));
    RECT measure{0,0,width-S(24,s),0};
    ::DrawTextW(dc,s.hoveredPath.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
    const int pathHeight=std::min<int>(measure.bottom,S(130,s)),height=pathHeight+S(100,s);
    const int x=std::clamp<int>(s.pointer.x+S(15,s),S(8,s),std::max<int>(S(8,s),s.rcClient.right-width-S(8,s)));
    const int y=std::clamp<int>(s.pointer.y+S(20,s),S(8,s),std::max<int>(S(8,s),s.rcClient.bottom-height-S(8,s)));
    RECT box{x,y,x+width,y+height};fillWith(dc,box,s.palette.panel);frameWith(dc,box,s.palette.border);
    RECT r{x+S(12,s),y+S(10,s),x+width-S(12,s),y+S(32,s)};
    drawTextIn(dc,s.hovered->name,r,s.palette.text,DT_END_ELLIPSIS);
    r.top+=S(27,s);r.bottom=r.top+pathHeight;::SetTextColor(dc,s.palette.muted);
    ::DrawTextW(dc,s.hoveredPath.c_str(),-1,&r,DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
    r.top=r.bottom+S(8,s);r.bottom=r.top+S(20,s);
    drawTextIn(dc,(s.hovered->dir?(s.spec.zh?L"文件夹 · ":L"Folder · "):(s.spec.zh?L"文件 · ":L"File · "))+sizeLabel(s,*s.hovered)+(sizeUnknown(*s.hovered)?L"":L" · "+share(s,s.hovered->logical)),r,s.palette.text,DT_END_ELLIPSIS);
    r.top+=S(24,s);r.bottom+=S(24,s);drawTextIn(dc,s.spec.zh?L"单击固定详情 · 双击进入文件夹":L"Click to select · double-click folders",r,s.palette.muted,DT_END_ELLIPSIS);
}


inline void drawWelcome(ViewerState& s, HDC hdc)
{
    const bool zh = s.spec.zh;
    RECT r = s.rcChart;
    ::InflateRect(&r, -S(24, s), -S(24, s));
    RECT tr = r;
    tr.bottom = r.top + S(34, s);
    HGDIOBJ old = ::SelectObject(hdc, s.fontBold);
    drawTextIn(hdc, zh ? L"磁盘透视 Disk View" : L"Disk View", tr, s.palette.text, DT_LEFT);
    ::SelectObject(hdc, old);

    RECT hr = r;
    hr.top = r.top + S(36, s);
    hr.bottom = hr.top + S(18, s);
    drawTextIn(hdc, zh ? L"输入任意路径回车扫描；或选一个盘开始。单击选中、双击进入；Backspace 返回，Ctrl+L 编辑路径。"
                       : L"Type a path and press Enter, or pick a drive. Double-click a folder to enter, "
                         L"Backspace goes up, Ctrl+L edits the path.",
               hr, s.palette.muted, DT_LEFT);

    // One card per fixed drive: letter, a usage bar, and the numbers. Clicking
    // a card scans that drive. The rects live in the state for hit-testing.
    s.driveRects.clear();
    const int cardW = std::min<int>(S(290, s), r.right - r.left), cardH = S(64, s), gap = S(12, s);
    int x = r.left, y = hr.bottom + S(20, s);
    for (std::size_t i = 0; i < s.driveCards.size(); ++i) {
        const Drive& d = s.driveCards[i];
        if (x + cardW > r.right) {
            x = r.left;
            y += cardH + gap;
        }
        if (y + cardH > r.bottom)
            break;
        const RECT card {x, y, x + cardW, y + cardH};
        fillWith(hdc, card, s.palette.panel);
        frameWith(hdc, card, s.hoverDrive >= 0 && static_cast<int>(i) == s.hoverDrive
                                 ? s.palette.accent
                                 : s.palette.border);
        RECT cr = card;
        ::InflateRect(&cr, -S(10, s), -S(8, s));
        // "C:  818 GB / 1.8 TB · 43%"
        RECT lr = cr;
        lr.bottom = lr.top + S(16, s);
        const std::wstring label = std::wstring(1, d.letter) + L":";
        const std::wstring numbers = winshun::wide(formatBytes(d.used(), zh)) + L" / "
                                     + winshun::wide(formatBytes(d.total, zh)) + L" · "
                                     + std::to_wstring(d.total > 0 ? d.used() * 100 / d.total : 0) + L"%";
        old = ::SelectObject(hdc, s.fontBold);
        drawTextIn(hdc, label, lr, s.palette.text, DT_LEFT);
        ::SelectObject(hdc, old);
        RECT nr = lr;
        nr.left = cr.left + S(30, s);
        drawTextIn(hdc, numbers, nr, s.palette.muted, DT_RIGHT);
        // The usage bar: filled part in the accent, the rest a border colour.
        RECT bar = cr;
        bar.top = lr.bottom + S(8, s);
        bar.bottom = bar.top + S(10, s);
        RECT fill = bar;
        const unsigned percent = d.total > 0 ? static_cast<unsigned>(d.used() * 100 / d.total) : 0;
        fill.right = bar.left + (bar.right - bar.left) * std::min(percent, 100u) / 100;
        fillWith(hdc, bar, s.palette.border);
        if (fill.right > fill.left)
            fillWith(hdc, fill, percent >= 90 ? s.palette.danger : s.palette.accent);
        s.driveRects.push_back(card);
        x += cardW + gap;
    }
}

inline void drawScanError(ViewerState& s, HDC hdc)
{
    const bool zh = s.spec.zh;
    RECT r = s.rcChart;
    ::InflateRect(&r, -S(24, s), -S(24, s));
    RECT tr = r;
    tr.bottom = r.top + S(34, s);
    HGDIOBJ old = ::SelectObject(hdc, s.fontBold);
    drawTextIn(hdc, zh ? L"扫描失败" : L"Scan failed", tr, s.palette.danger, DT_LEFT);
    ::SelectObject(hdc, old);
    RECT er = r;
    er.top = r.top + S(44, s);
    ::SetTextColor(hdc, s.palette.text);
    ::DrawTextW(hdc, winshun::wide(s.lastScanError).c_str(), -1, &er, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}


inline void paintAll(ViewerState& s,HDC dc,const RECT& client) {
    s.driveRects.clear();const auto old=::SelectObject(dc,s.font);fillWith(dc,client,s.palette.bg);
    fillWith(dc,{0,0,client.right,S(44,s)},s.palette.panel);
    drawTextIn(dc,L"◩  Disk View  "+std::wstring(s.spec.zh?L"磁盘透视":L""),{S(16,s),0,s.rcTheme.left-S(10,s),S(44,s)},s.palette.text,DT_VCENTER);
    drawButton(s,dc,s.rcTheme,s.palette.dark?(s.spec.zh?L"浅色":L"Light"):(s.spec.zh?L"深色":L"Dark"),false,false);
    drawButton(s,dc,s.rcHistBack,L"←",false,false);drawButton(s,dc,s.rcHistForward,L"→",false,false);drawButton(s,dc,s.rcBack,L"↑",false,false);
    drawButton(s,dc,s.rcScan,s.scanning?(s.spec.zh?L"取消扫描":L"Cancel"):(s.pendingPath.empty()?(s.spec.zh?L"重新扫描":L"Scan"):(s.spec.zh?L"扫描此路径":L"Scan path")),!s.scanning,s.hoverScan);
    drawBreadcrumb(s,dc);
    drawButton(s,dc,s.rcDrive,s.tree?s.tree->name.substr(0,2)+L" ▾":(s.spec.zh?L"选择磁盘 ▾":L"Drive ▾"),false,false);
    const std::wstring source=s.resultIncomplete?(s.spec.zh?L"本轮未完成结果":L"Incomplete result"):s.scanning?(s.spec.zh?L"上次完整快照":L"Previous snapshot"):(s.spec.zh?L"完整快照":L"Complete snapshot");
    drawTextIn(dc,(s.tree?source+(s.fastResult?L" · MFT · ":(s.spec.zh?L" · 普通 · ":L" · Standard · ")):L"")+(s.spec.zh?L"逻辑大小 ⓘ":L"Logical size ⓘ"),s.rcAccounting,s.resultIncomplete?s.palette.danger:s.palette.accent,DT_VCENTER|DT_END_ELLIPSIS);
    drawTextIn(dc,std::to_wstring(s.skippedDirs+s.unknownSizes+s.orphanRecords+(!s.lastScanError.empty()?1:0))+(s.spec.zh?L" 个扫描问题":L" scan issues"),s.rcSkipped,s.palette.accent,DT_RIGHT|DT_VCENTER|DT_END_ELLIPSIS);
    drawProgressBox(s,dc);
    if(!s.notice.empty()){RECT r=s.rcOverview;r.top-=S(28,s);r.bottom=r.top+S(24,s);drawTextIn(dc,s.notice,r,s.palette.danger,DT_VCENTER|DT_END_ELLIPSIS);}
    if(s.tree&&s.viewRoot) {
        const auto f=::SelectObject(dc,s.fontHeading);
        drawTextIn(dc,s.viewRoot->name+L"   "+winshun::wide(formatBytes(s.viewRoot->logical,s.spec.zh))+(s.resultIncomplete?(s.spec.zh?L" · 当前已发现":L" · discovered so far"):L""),s.rcOverview,s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);::SelectObject(dc,f);
        if(s.narrow){drawButton(s,dc,s.rcShowChart,s.spec.zh?L"空间图":L"Chart",!s.mobileList,false);drawButton(s,dc,s.rcShowList,s.spec.zh?L"目录列表":L"List",s.mobileList,false);}
        if(s.rcChartPane.right>s.rcChartPane.left) {
            fillWith(dc,s.rcChartPane,s.palette.panel);frameWith(dc,s.rcChartPane,s.palette.border);
            RECT title=s.rcChartPane;title.left+=S(12,s);title.bottom=title.top+S(48,s);title.right=s.rcTreemapBtn.left-S(8,s);
            drawTextIn(dc,s.spec.zh?L"空间分布":L"Space distribution",title,s.palette.text,DT_VCENTER|DT_END_ELLIPSIS);
            drawButton(s,dc,s.rcTreemapBtn,s.spec.zh?L"方块":L"Tiles",!s.sunburst,false);drawButton(s,dc,s.rcSunburstBtn,s.spec.zh?L"圆盘":L"Rings",s.sunburst,false);
            drawButton(s,dc,s.rcDepth,s.depth==1?(s.spec.zh?L"展开：当前一层":L"Depth: 1"):(s.spec.zh?L"展开：两层":L"Depth: 2"),false,false);
            RECT hint{s.rcDepth.right+S(10,s),s.rcDepth.top,s.rcChartPane.right-S(12,s),s.rcDepth.bottom};
            drawTextIn(dc,s.spec.zh?L"单击选中 · 双击进入":L"Click select · double-click enter",hint,s.palette.muted,DT_RIGHT|DT_VCENTER|DT_END_ELLIPSIS);
            const int saved=::SaveDC(dc);::IntersectClipRect(dc,s.rcChart.left,s.rcChart.top,s.rcChart.right,s.rcChart.bottom);
            if(s.sunburst)drawSlices(s,dc);else drawTiles(s,dc);
            if(!s.viewRoot->logical)drawTextIn(dc,s.spec.zh?L"暂无可绘制大小，请查看列表":L"No sized entries; use the list",s.rcChart,s.palette.muted,DT_CENTER|DT_VCENTER);
            ::RestoreDC(dc,saved);
            RECT legend{s.rcChartPane.left+S(12,s),s.rcChartPane.bottom-S(28,s),s.rcChartPane.right-S(12,s),s.rcChartPane.bottom};
            drawTextIn(dc,s.spec.zh?L"文件夹  ·  文档  ·  视频  ·  压缩包  ·  其他":L"Folders · Documents · Media · Archives · Other",legend,s.palette.muted,DT_VCENTER|DT_END_ELLIPSIS);
        }
        if(s.rcDivider.right>s.rcDivider.left)fillWith(dc,s.rcDivider,s.palette.border);
        if(s.rcTopList.right>s.rcTopList.left)drawTopList(s,dc);
    } else if(!s.lastScanError.empty())drawScanError(s,dc);
    else if(!s.scanning)drawWelcome(s,dc);
    drawStatusRows(s,dc);drawKeyboardFocus(s,dc);drawTooltip(s,dc);::SelectObject(dc,old);
}


// ---- hit testing --------------------------------------------------------------------

inline int hitTile(const ViewerState& s, int x, int y)
{
    if (!inRect(s.rcChart, x, y))
        return -1;
    // Children are appended after their parent, so the reverse scan finds the
    // innermost (topmost) tile first.
    for (std::size_t i = s.tiles.size(); i-- > 0;)
        if (s.tiles[i].rect.contains(static_cast<double>(x), static_cast<double>(y)))
            return static_cast<int>(i);
    return -1;
}

inline int hitSlice(const ViewerState& s, int x, int y)
{
    if (!inRect(s.rcChart, x, y) || s.slices.empty())
        return -1;
    const double cx = (s.rcChart.left + s.rcChart.right) / 2.0;
    const double cy = (s.rcChart.top + s.rcChart.bottom) / 2.0;
    const double dx = x - cx, dy = y - cy;
    const double r = std::sqrt(dx * dx + dy * dy);
    double a = std::atan2(dy, dx);
    if (a < -3.14159265358979323846 / 2)
        a += 6.28318530717958647692; // the layout spans [-π/2, 3π/2)
    for (std::size_t i = s.slices.size(); i-- > 0;) {
        const Slice& sl = s.slices[i];
        if (r >= sl.r0 && r <= sl.r1 && a >= sl.a0 && a <= sl.a1)
            return static_cast<int>(i);
    }
    return -1;
}

inline int hitTop(const ViewerState& s, int x, int y)
{
    if (x < s.rcTopList.left || x >= s.rcTopList.right || y < s.rcTopList.top + s.topHeaderH
        || y >= s.rcTopList.bottom)
        return -1;
    const int row = (y - s.rcTopList.top - s.topHeaderH) / s.rowH;
    if (row < 0 || row >= maxRows(s))
        return -1;
    const int idx = row + s.topScroll;
    return idx >= 0 && idx < listCount(s) ? idx : -1;
}

inline int hitCrumb(const ViewerState& s, int x, int y)
{
    for (std::size_t i = 0; i < s.crumbRects.size(); ++i)
        if (inRect(s.crumbRects[i], x, y))
            return static_cast<int>(i);
    return -1;
}

inline int hitDrive(const ViewerState& s, int x, int y)
{
    if (s.tree || s.scanning || !s.lastScanError.empty())
        return -1;
    for (std::size_t i = 0; i < s.driveRects.size(); ++i)
        if (inRect(s.driveRects[i], x, y))
            return static_cast<int>(i);
    return -1;
}

inline bool anyClickable(const ViewerState& s, int x, int y)
{
    for(const auto* r:{&s.rcHistBack,&s.rcHistForward,&s.rcAddressEdit,&s.rcDrive,&s.rcAccounting,&s.rcTheme,&s.rcPreview,&s.rcDepth,&s.rcShowChart,&s.rcShowList,&s.rcEnter,&s.rcLocate,&s.rcSortName,&s.rcSortSize,&s.rcPrevious,&s.rcNext})if(inRect(*r,x,y))return true;
    return inRect(s.rcScan, x, y) || inRect(s.rcBack, x, y) || inRect(s.rcTreemapBtn, x, y)
           || inRect(s.rcSunburstBtn, x, y) || hitTop(s, x, y) >= 0 || hitCrumb(s, x, y) >= 0
           || inRect(s.rcListFiles, x, y) || inRect(s.rcListFolders, x, y)
           || inRect(s.rcSkipped, x, y)
           || hitTile(s, x, y) >= 0 || hitSlice(s, x, y) >= 0 || hitDrive(s, x, y) >= 0;
}

// ---- navigation ---------------------------------------------------------------------

// Walks from the scan root along `path` (the display form). `chain` receives
// every descended node, root excluded. Returns nullptr when the path does not
// exist in the tree.
inline const Node* descend(ViewerState& s, const std::wstring& path, std::vector<const Node*>* chain)
{
    if (chain)
        chain->clear();
    if (!s.tree)
        return nullptr;
    if (lower(path) == lower(s.tree->name))
        return s.tree.get();
    const std::wstring prefix = joinPath(s.tree->name, L"");
    if (lower(path).rfind(lower(prefix), 0) != 0)
        return nullptr;
    const Node* n = s.tree.get();
    std::size_t pos = prefix.size();
    std::vector<const Node*> found;
    for (;;) {
        const std::size_t slash = path.find(L'\\', pos);
        const std::size_t end = slash == std::wstring::npos ? path.size() : slash;
        const auto part=lower(path.substr(pos,end-pos));
        const Node* foundNode=nullptr;
        for(const auto& child:n->children)if(lower(child.name)==part){foundNode=&child;break;}
        n=foundNode;
        if (!n)
            return nullptr;
        found.push_back(n);
        if (slash == std::wstring::npos) {
            if (chain)
                *chain = std::move(found);
            return n;
        }
        pos = slash + 1;
    }
}


inline void clearHover(ViewerState& s) {
    s.hovered=nullptr;s.hoveredPath.clear();s.tipVisible=false;
    s.hoverTile=s.hoverSlice=s.hoverTop=s.hoverFolder=-1;if(s.hwnd)::KillTimer(s.hwnd,2);
}
inline bool navigateTo(ViewerState& s,const std::wstring& path) {
    std::vector<const Node*> chain;const Node* folder=descend(s,path,&chain);
    if(!folder||!folder->dir)return false;
    if(!chain.empty())chain.pop_back();s.drill=std::move(chain);s.viewRoot=folder;
    s.selected=nullptr;s.selectedPath.clear();s.filter.clear();s.groupFilter.clear();
    if(s.filterEdit)::SetWindowTextW(s.filterEdit,L"");
    const auto canonical=nodePath(s,folder);
    if(!s.replayHistory&&(s.historyIndex<0||s.history[s.historyIndex]!=canonical)) {
        s.history.resize(static_cast<std::size_t>(s.historyIndex+1));s.history.push_back(canonical);s.historyIndex=int(s.history.size())-1;
    }
    if(s.edit)::SetWindowTextW(s.edit,canonical.c_str());
    s.editingAddress=false;rebuildLists(s);relayoutView(s);
    if(s.hwnd){computeLayout(s);::InvalidateRect(s.hwnd,nullptr,FALSE);}return true;
}
inline void goUp(ViewerState& s) {
    if(!s.viewRoot||s.viewRoot==s.tree.get())return;
    const auto path=nodePath(s,s.viewRoot);const auto slash=path.find_last_of(L'\\');
    navigateTo(s,slash<s.tree->name.size()?s.tree->name:path.substr(0,slash));
}
inline void historyStep(ViewerState& s,int step) {
    const int next=s.historyIndex+step;if(next<0||next>=int(s.history.size()))return;
    const auto path=s.history[next];s.replayHistory=true;
    if(navigateTo(s,path))s.historyIndex=next;s.replayHistory=false;
}
inline void drillTo(ViewerState& s,int index) {
    if(index<0||index>=int(s.crumbNodes.size()))return;
    const Node* target=s.crumbNodes[index];
    std::wstring path=s.tree->name;
    if(target!=s.tree.get()) {
        for(auto n:s.drill){path=childPath(path,n->name);if(n==target)break;}
        if(target==s.viewRoot)path=nodePath(s,target);
    }
    navigateTo(s,path);
}
inline void setViewMode(ViewerState& s,bool sunburst){s.sunburst=sunburst;relayoutView(s);if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);}
inline void setListMode(ViewerState& s,bool folders){s.listFolders=folders;s.groupFilter.clear();clearHover(s);rebuildLists(s);if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);}
inline void selectNode(ViewerState& s,const Node* n,const std::wstring& path) {
    for(const auto& group:s.groupMembers)if(group.first==n) {
        // Navigation rebuilds the synthetic groups. Copy members and parent
        // path first so a second-level group cannot leave dangling pointers.
        auto members=group.second;
        const auto slash=path.find_last_of(L'\\');
        const auto parent=slash<s.tree->name.size()?s.tree->name:path.substr(0,slash);
        if(!navigateTo(s,parent))return;
        s.groupFilter=std::move(members);s.listFolders=true;s.mobileList=true;s.filter.clear();
        if(s.filterEdit)::SetWindowTextW(s.filterEdit,L"");
        refreshRows(s);computeLayout(s);relayoutView(s);clearHover(s);
        if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);
        return;
    }
    s.selected=n;s.selectedPath=path;if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline bool openListRow(ViewerState& s,int index) {
    if(index<0||index>=listCount(s))return false;const auto item=s.rows[index];
    if(item.node->dir)return navigateTo(s,item.path);
    selectNode(s,item.node,item.path);return true;
}
inline void locateSelected(ViewerState& s) {
    if(!s.selected)return;const auto path=s.selectedPath;const auto slash=path.find_last_of(L'\\');
    const auto parent=slash<s.tree->name.size()?s.tree->name:path.substr(0,slash);
    if(navigateTo(s,parent)){selectNode(s,descend(s,path,nullptr),path);s.mobileList=false;computeLayout(s);relayoutView(s);}
}
inline void updateHover(ViewerState& s,int x,int y) {
    s.pointer={x,y};s.hoverTile=s.sunburst?-1:hitTile(s,x,y);s.hoverSlice=s.sunburst?hitSlice(s,x,y):-1;
    const int row=hitTop(s,x,y);s.hoverTop=s.listFolders?-1:row;s.hoverFolder=s.listFolders?row:-1;
    s.hoverCrumb=hitCrumb(s,x,y);s.hoverDrive=hitDrive(s,x,y);
    const Node* n=nullptr;std::wstring path;
    if(s.hoverTile>=0){const auto& t=s.tiles[s.hoverTile];n=t.node;path=t.path;}
    else if(s.hoverSlice>=0){const auto& t=s.slices[s.hoverSlice];n=t.node;path=t.path;}
    else if(row>=0){n=s.rows[row].node;path=s.rows[row].path;}
    if(n!=s.hovered||path!=s.hoveredPath) {
        s.hovered=n;s.hoveredPath=path;s.tipVisible=false;
        if(s.hwnd){::KillTimer(s.hwnd,2);if(n)::SetTimer(s.hwnd,2,250,nullptr);}
    }
    if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);
}


// ---- message handling ---------------------------------------------------------------

inline std::wstring editText(HWND edit)
{
    const int len = ::GetWindowTextLengthW(edit);
    if (len <= 0)
        return {};
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    ::GetWindowTextW(edit, text.data(), len + 1);
    text.resize(static_cast<std::size_t>(len));
    return text;
}


inline void editAddress(ViewerState& s) {
    s.editingAddress=true;
    if(s.viewRoot)::SetWindowTextW(s.edit,nodePath(s,s.viewRoot).c_str());
    computeLayout(s);::SetFocus(s.edit);::SendMessageW(s.edit,EM_SETSEL,0,-1);
}
inline void submitAddress(ViewerState& s) {
    const auto path=normalizePath(editText(s.edit),nullptr);
    if(path.empty()){s.notice=s.spec.zh?L"请输入有效路径":L"Enter a valid path";return;}
    if(navigateTo(s,path)){s.pendingPath.clear();s.notice.clear();::SetFocus(s.hwnd);}
    else if(!s.tree)startScan(s,path);
    else {s.pendingPath=path;s.notice=s.spec.zh?L"该路径尚未索引；点击“扫描此路径”开始。":L"Path not indexed; choose Scan this path.";}
    computeLayout(s);relayoutView(s);::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline void adoptResult(ViewerState& s,const std::shared_ptr<ScanResult>& r,bool incomplete) {
    const auto previousRoot=s.tree?s.tree->name:std::wstring();
    const auto path=s.viewRoot?nodePath(s,s.viewRoot):std::wstring();
    const auto selection=s.selectedPath,query=s.filter;const int scroll=s.topScroll;
    s.selected=nullptr;s.hovered=nullptr;s.viewRoot=nullptr;s.drill.clear();s.groupFilter.clear();
    s.tiles.clear();s.slices.clear();s.groups.clear();s.crumbNodes.clear();
    s.tree=std::shared_ptr<Node>(r,&r->root);s.viewRoot=s.tree.get();s.resultIncomplete=incomplete;
    s.fastResult=r->fast;s.unknownSizes=r->unknownSizes;s.orphanRecords=r->orphanRecords;
    s.rootLargest=r->largest;s.skippedList=r->skippedList;s.files=r->files;s.dirs=r->dirs;
    s.skippedDirs=r->skippedDirs;s.skippedLinks=r->skippedLinks;s.elapsedMs=r->elapsedMs;
    s.replayHistory=true;
    if(!navigateTo(s,path))navigateTo(s,s.tree->name);
    s.replayHistory=false;
    if(s.history.empty() || lower(previousRoot)!=lower(s.tree->name)){s.history={nodePath(s,s.viewRoot)};s.historyIndex=0;}
    s.selected=descend(s,selection,nullptr);s.selectedPath=s.selected?selection:L"";
    s.filter=query;if(s.filterEdit)::SetWindowTextW(s.filterEdit,query.c_str());refreshRows(s);s.topScroll=scroll;
    computeLayout(s);relayoutView(s);
}
inline void showIssues(ViewerState& s) {
    std::wstring text;
    if(!s.lastScanError.empty())text+=(s.spec.zh?L"本次扫描失败：\r\n":L"Latest scan failed:\r\n")+winshun::wide(s.lastScanError)+L"\r\n\r\n";
    for(const auto& dir:s.skippedList)text+=dir.path+L"  ("+std::to_wstring(dir.error)+L")\r\n";
    if(s.skippedDirs>s.skippedList.size())text+=(s.spec.zh?L"另有未列出目录：":L"More directories: ")+std::to_wstring(s.skippedDirs-s.skippedList.size());
    if(text.empty())text=s.spec.zh?L"没有记录到读取失败的目录。":L"No directory read errors recorded.";
    if(s.unknownSizes)text+=L"\r\n"+std::to_wstring(s.unknownSizes)+(s.spec.zh?L" 个文件大小未知。":L" unknown file sizes.");
    if(s.orphanRecords)text+=L"\r\n"+std::to_wstring(s.orphanRecords)+(s.spec.zh?L" 条记录未能归属目录（包含未跟随的链接和扫描期间变更）。":L" unlinked records (including reparse targets and concurrent changes).");
    if(s.skippedLinks)text+=L"\r\n"+std::to_wstring(s.skippedLinks)+(s.spec.zh?L" 条重解析链接未跟随。":L" reparse links were not followed.");
    ::MessageBoxW(s.hwnd,text.c_str(),s.spec.zh?L"扫描覆盖范围":L"Scan coverage",MB_OK|MB_ICONINFORMATION);
}
inline void chooseDrive(ViewerState& s) {
    auto drives=listFixedDrives();HMENU menu=::CreatePopupMenu();
    for(std::size_t i=0;i<drives.size();++i)::AppendMenuW(menu,MF_STRING,100+i,drives[i].root.c_str());
    POINT pt{s.rcDrive.left,s.rcDrive.bottom};::ClientToScreen(s.hwnd,&pt);
    const int choice=::TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,s.hwnd,nullptr);::DestroyMenu(menu);
    if(choice>=100&&choice<100+int(drives.size()))startScan(s,drives[choice-100].root);
}
inline void activateAt(ViewerState& s,int x,int y,bool twice=false) {
    if(twice) {
        const int row=hitTop(s,x,y);if(row>=0){openListRow(s,row);return;}
        const int hit=s.sunburst?hitSlice(s,x,y):hitTile(s,x,y);
        if(hit>=0){const auto* n=s.sunburst?s.slices[hit].node:s.tiles[hit].node;const auto path=s.sunburst?s.slices[hit].path:s.tiles[hit].path;if(n->dir)navigateTo(s,path);}
        return;
    }
    if(inRect(s.rcScan,x,y)) {if(s.scanning)s.cancelFlag=true;else startScan(s,!s.pendingPath.empty()?s.pendingPath:!s.lastPath.empty()?s.lastPath:editText(s.edit));return;}
    if(inRect(s.rcHistBack,x,y)){historyStep(s,-1);return;}
    if(inRect(s.rcHistForward,x,y)){historyStep(s,1);return;}
    if(inRect(s.rcBack,x,y)){goUp(s);return;}
    if(inRect(s.rcAddressEdit,x,y)){editAddress(s);return;}
    if(inRect(s.rcDrive,x,y)){chooseDrive(s);return;}
    if(inRect(s.rcTheme,x,y)) {
        s.palette=s.palette.dark?lightPalette():darkPalette();if(s.editBrush){::DeleteObject(s.editBrush);s.editBrush=nullptr;}
        if(s.filterEdit)::InvalidateRect(s.filterEdit,nullptr,TRUE);if(s.edit)::InvalidateRect(s.edit,nullptr,TRUE);
    } else if(inRect(s.rcSkipped,x,y)){showIssues(s);return;}
    else if(inRect(s.rcAccounting,x,y)) {
        ::MessageBoxW(s.hwnd,s.spec.zh?L"图表按逻辑大小绘制（1024 进制）。\r\n按簇取整只是占用空间近似值：稀疏、压缩文件及硬链接会影响结果。\r\n无法读取的区域和系统元数据可能未计入，因此文件合计不等于磁盘已用空间。\r\n局部结果占比只以本轮已发现的条目为基准。":L"Charts show logical bytes (base 1024).\r\nCluster-rounded allocation is approximate; sparse files, compression and hard links affect totals.\r\nUnreadable areas and filesystem metadata may be absent.\r\nPartial-result shares use discovered entries only.",s.spec.zh?L"容量统计口径":L"Size accounting",MB_OK|MB_ICONINFORMATION);return;
    } else if(inRect(s.rcPreview,x,y)&&s.scanning) {
        if(s.partialView&&s.completeResult){s.partialView=false;adoptResult(s,s.completeResult,false);}
        else if(s.partialResult){s.partialView=true;adoptResult(s,s.partialResult,true);}
        else s.notice=s.spec.zh?L"正在准备本轮局部结果…":L"Preparing partial results…";
    } else if(inRect(s.rcTreemapBtn,x,y)){setViewMode(s,false);return;}
    else if(inRect(s.rcSunburstBtn,x,y)){setViewMode(s,true);return;}
    else if(inRect(s.rcDepth,x,y)){s.depth=s.depth==1?2:1;relayoutView(s);}
    else if(inRect(s.rcListFiles,x,y)||inRect(s.rcListFolders,x,y)){setListMode(s,inRect(s.rcListFolders,x,y));return;}
    else if(inRect(s.rcShowChart,x,y)||inRect(s.rcShowList,x,y)){s.mobileList=inRect(s.rcShowList,x,y);computeLayout(s);relayoutView(s);}
    else if(inRect(s.rcEnter,x,y)){if(s.selected&&s.selected->dir)navigateTo(s,s.selectedPath);return;}
    else if(inRect(s.rcLocate,x,y)){locateSelected(s);return;}
    else if(inRect(s.rcSortName,x,y)||inRect(s.rcSortSize,x,y)) {
        const bool name=inRect(s.rcSortName,x,y);s.ascending=s.sortName==name?!s.ascending:name;s.sortName=name;clearHover(s);refreshRows(s);
    } else if(inRect(s.rcPrevious,x,y)||inRect(s.rcNext,x,y)) {
        s.topScroll=std::clamp(s.topScroll+(inRect(s.rcNext,x,y)?1:-1)*std::max(1,maxRows(s)),0,std::max(0,listCount(s)-maxRows(s)));clearHover(s);
    } else if(const int crumb=hitCrumb(s,x,y);crumb>=0){drillTo(s,crumb);return;}
    else if(const int drive=hitDrive(s,x,y);drive>=0){startScan(s,s.driveCards[drive].root);return;}
    else if(const int row=hitTop(s,x,y);row>=0){const auto item=s.rows[row];selectNode(s,item.node,item.path);return;}
    else {
        const int hit=s.sunburst?hitSlice(s,x,y):hitTile(s,x,y);
        if(hit>=0){const auto* n=s.sunburst?s.slices[hit].node:s.tiles[hit].node;const auto path=s.sunburst?s.slices[hit].path:s.tiles[hit].path;selectNode(s,n,path);return;}
    }
    ::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline LRESULT CALLBACK editProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    ViewerState& s=state();
    if(msg==WM_CHAR&&(wp==VK_TAB||wp==VK_RETURN||wp==VK_ESCAPE))return 0;
    if(msg==WM_KEYDOWN) {
        if(wp==VK_TAB){cycleFocus(s,(::GetKeyState(VK_SHIFT)&0x8000)?-1:1);return 0;}
        if(wp==VK_RETURN){submitAddress(s);return 0;}
        if(wp==VK_ESCAPE){s.editingAddress=false;s.pendingPath.clear();s.notice.clear();if(s.viewRoot)::SetWindowTextW(s.edit,nodePath(s,s.viewRoot).c_str());computeLayout(s);::SetFocus(s.hwnd);::InvalidateRect(s.hwnd,nullptr,FALSE);return 0;}
        if(wp==VK_F5){if(!s.scanning)startScan(s,s.lastPath);return 0;}
    }
    return ::CallWindowProcW(s.origEditProc,hwnd,msg,wp,lp);
}
inline LRESULT CALLBACK filterProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto& s=state();
    if(msg==WM_PAINT||msg==WM_PRINT||msg==WM_PRINTCLIENT) {
        const auto result=::CallWindowProcW(s.origFilterProc,hwnd,msg,wp,lp);
        if(::GetWindowTextLengthW(hwnd)==0) {
            const auto dc=msg==WM_PAINT?::GetDC(hwnd):reinterpret_cast<HDC>(wp);
            const int saved=::SaveDC(dc);
            RECT r{};::GetClientRect(hwnd,&r);r.left+=S(6,s);
            ::SelectObject(dc,s.font);::SetBkMode(dc,TRANSPARENT);
            drawTextIn(dc,s.spec.zh?L"筛选列表，图表保持完整":L"Filter list; chart stays complete",r,s.palette.muted,DT_VCENTER|DT_END_ELLIPSIS);
            ::RestoreDC(dc,saved);
            if(msg==WM_PAINT)::ReleaseDC(hwnd,dc);
        }
        return result;
    }
    if(msg==WM_CHAR&&(wp==VK_TAB||wp==VK_RETURN||wp==VK_ESCAPE))return 0;
    if(msg==WM_KEYDOWN&&wp==VK_TAB){cycleFocus(s,(::GetKeyState(VK_SHIFT)&0x8000)?-1:1);return 0;}
    if(msg==WM_KEYDOWN&&wp==VK_RETURN){::SetFocus(s.hwnd);return 0;}
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){::SetWindowTextW(hwnd,L"");::SetFocus(s.hwnd);return 0;}
    if(msg==WM_KEYDOWN&&wp=='L'&&(::GetKeyState(VK_CONTROL)&0x8000)){editAddress(s);return 0;}
    return ::CallWindowProcW(s.origFilterProc,hwnd,msg,wp,lp);
}


inline LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ViewerState& s = state();
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        RECT client;
        ::GetClientRect(hwnd, &client);
        const int w = static_cast<int>(std::max<LONG>(1, client.right - client.left));
        const int h = static_cast<int>(std::max<LONG>(1, client.bottom - client.top));
        HDC mem = ::CreateCompatibleDC(dc);
        // Transparent text background: otherwise DrawTextW paints an opaque
        // rectangle (the DC background colour) behind every string.
        ::SetBkMode(mem, TRANSPARENT);
        HBITMAP bmp = ::CreateCompatibleBitmap(dc, w, h);
        HBITMAP oldBmp = static_cast<HBITMAP>(::SelectObject(mem, bmp));
        paintAll(s, mem, client);
        ::BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        computeLayout(s);
        relayoutView(s);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        if(wp==2){::KillTimer(hwnd,2);s.tipVisible=s.hovered!=nullptr;}
        ::InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_COMMAND:
        if(reinterpret_cast<HWND>(lp)==s.filterEdit&&HIWORD(wp)==EN_CHANGE) {
            s.filter=editText(s.filterEdit);clearHover(s);refreshRows(s);::InvalidateRect(hwnd,nullptr,FALSE);return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(s.dragging){s.splitRatio=std::clamp(double(x-S(16,s))/std::max(1L,s.rcClient.right-S(32,s)),.38,.69);computeLayout(s);relayoutView(s);::InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,hwnd,0};::TrackMouseEvent(&track);updateHover(s,x,y);return 0;
    }
    case WM_MOUSELEAVE:clearHover(s);::InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:
        s.keyboardFocus=false;
        if(inRect(s.rcDivider,GET_X_LPARAM(lp),GET_Y_LPARAM(lp))){s.dragging=true;clearHover(s);::SetCapture(hwnd);}return 0;
    case WM_CAPTURECHANGED:s.dragging=false;return 0;
    case WM_LBUTTONDBLCLK:
        s.suppressClick=true;activateAt(s,GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;
    case WM_LBUTTONUP:
        s.keyboardFocus=false;
        if(s.suppressClick){s.suppressClick=false;return 0;}
        if(s.dragging){s.dragging=false;::ReleaseCapture();return 0;}
        ::SetFocus(hwnd);activateAt(s,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;


    case WM_MOUSEWHEEL: {
        if (s.rcTopList.right <= s.rcTopList.left)
            return 0;
        POINT p {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &p);
        if (!inRect(s.rcTopList, p.x, p.y))
            return 0;
        const int rows = maxRows(s);
        const int maxScroll = std::max(0, listCount(s) - rows);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        s.topScroll = std::clamp(s.topScroll - (delta / WHEEL_DELTA) * 3, 0, maxScroll);
        updateHover(s, p.x, p.y);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SYSKEYDOWN:
        if(wp==VK_LEFT){historyStep(s,-1);return 0;}
        if(wp==VK_RIGHT){historyStep(s,1);return 0;}
        if(wp==VK_UP){goUp(s);return 0;}
        break;
    case WM_KEYDOWN:
        if(wp==VK_TAB){cycleFocus(s,(::GetKeyState(VK_SHIFT)&0x8000)?-1:1);return 0;}
        if((wp==VK_SPACE||wp==VK_RETURN)&&s.keyboardFocus) {
            const auto targets=focusTargets(s);
            if(s.focusIndex>=0&&s.focusIndex<int(targets.size())) {
                const auto target=targets[s.focusIndex];
                if(!target.child&&!target.list){activateAt(s,(target.rect.left+target.rect.right)/2,(target.rect.top+target.rect.bottom)/2);return 0;}
            }
        }
        if(wp=='L'&&(::GetKeyState(VK_CONTROL)&0x8000)){editAddress(s);return 0;}
        if(wp=='F'&&(::GetKeyState(VK_CONTROL)&0x8000)){s.mobileList=true;computeLayout(s);::SetFocus(s.filterEdit);return 0;}
        if(wp==VK_ESCAPE){clearHover(s);s.selected=nullptr;s.selectedPath.clear();::InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(wp==VK_BACK){goUp(s);return 0;}
        if(wp==VK_F5){if(!s.scanning)startScan(s,s.lastPath);return 0;}
        if(wp==VK_RETURN&&s.selected&&s.selected->dir){navigateTo(s,s.selectedPath);return 0;}
        if(wp==VK_UP||wp==VK_DOWN) {
            int index=-1;for(int i=0;i<listCount(s);++i)if(s.rows[i].node==s.selected)index=i;
            index=std::clamp(index+(wp==VK_DOWN?1:-1),0,std::max(0,listCount(s)-1));
            if(listCount(s)){selectNode(s,s.rows[index].node,s.rows[index].path);if(index<s.topScroll)s.topScroll=index;if(index>=s.topScroll+maxRows(s))s.topScroll=index-std::max(1,maxRows(s))+1;}return 0;
        }
        break;


    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            const DWORD pos = ::GetMessagePos();
            POINT p {GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
            ::ScreenToClient(hwnd, &p);
            ::SetCursor(inRect(s.rcDivider,p.x,p.y) ? ::LoadCursorW(nullptr,IDC_SIZEWE) : anyClickable(s, p.x, p.y) ? ::LoadCursorW(nullptr, IDC_HAND)
                                                  : ::LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == s.edit || reinterpret_cast<HWND>(lp) == s.filterEdit) {
            const COLORREF back = s.palette.input;
            if (!s.editBrush)
                s.editBrush = ::CreateSolidBrush(back);
            ::SetTextColor(reinterpret_cast<HDC>(wp), s.palette.text);
            ::SetBkColor(reinterpret_cast<HDC>(wp), back);
            return reinterpret_cast<LRESULT>(s.editBrush);
        }
        break;
    case WM_DPICHANGED: {
        s.dpi = HIWORD(wp);
        s.windowIcons.update(hwnd, s.dpi);
        makeFonts(s);
        const RECT* sug = reinterpret_cast<const RECT*>(lp);
        ::SetWindowPos(hwnd, nullptr, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PRINTCLIENT: {
        // PrintWindow (and other requesters) want the current frame on the
        // DC they hand over; draw the same thing we would into the window.
        ViewerState& st = s;
        if (!st.hwnd)
            return 0;
        RECT client;
        ::GetClientRect(hwnd, &client);
        ::SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        paintAll(st, reinterpret_cast<HDC>(wp), client);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {S(360, s), S(680, s)};
        return 0;
    }
    case WM_APP_APPEARANCE: {
        // The host's look changed (its theme or its style): the window takes
        // the new colours.
        winshun::Json appearance;
        {
            std::lock_guard<std::mutex> lk(s.m);
            appearance = s.pendingAppearance;
        }
        const std::string theme = appearance["theme"].isString() ? appearance["theme"].toString()
                                                                 : s.spec.theme;
        s.spec.theme = theme;
        s.palette = hostPalette(appearance, theme == "light" ? lightPalette() : darkPalette());
        if (s.editBrush) { // the path box's fill changed with the palette
            ::DeleteObject(s.editBrush);
            s.editBrush = nullptr;
        }
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_START_SCAN:
        startScan(s,s.spec.initialPath);return 0;
    case WM_APP_PROGRESS:
        if (lp) {
            s.progress = *reinterpret_cast<ScanProgress*>(lp);
            if(!s.progress.fallbackReason.empty()) {
                const auto notice=(s.spec.zh?L"已回退普通扫描：":L"Using standard scan: ")+s.progress.fallbackReason;
                if(notice!=s.notice){s.notice=notice;computeLayout(s);relayoutView(s);}
            }
            delete reinterpret_cast<ScanProgress*>(lp);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP_PARTIAL:
        if(lp) {
            std::unique_ptr<std::shared_ptr<ScanResult>> packet(reinterpret_cast<std::shared_ptr<ScanResult>*>(lp));
            if(s.scanning){s.partialResult=*packet;if(s.partialView||!s.tree)adoptResult(s,s.partialResult,true);::InvalidateRect(hwnd,nullptr,FALSE);}
        }
        return 0;
    case WM_APP_DONE:
        joinScanThread(s);
        if(lp) {
            std::shared_ptr<ScanResult> r(reinterpret_cast<ScanResult*>(lp));
            s.scanning=false;::KillTimer(hwnd,1);s.cancelled=r->cancelled;clearHover(s);
            if(!r->error.empty()||r->cancelled) {
                s.lastScanError=r->error;
                if(s.completeResult){adoptResult(s,s.completeResult,false);s.notice=r->cancelled?(s.spec.zh?L"扫描已取消，已保留上次完成结果。":L"Cancelled; previous result retained."):(s.spec.zh?L"更新失败，已保留上次完成结果。点击重新扫描重试。":L"Update failed; previous result retained.");}
                else if(s.partialResult && r->root.name.empty()){adoptResult(s,s.partialResult,true);s.notice=r->cancelled?(s.spec.zh?L"扫描已取消，仅保留已发现结果。":L"Cancelled; partial result retained."):(s.spec.zh?L"扫描失败，仅保留已发现结果。问题详情可查看失败原因。":L"Scan failed; partial result retained. See scan issues.");}
                else if(!r->root.name.empty()&&r->root.files){s.partialResult=r;adoptResult(s,r,true);s.notice=s.spec.zh?L"扫描未完成，仅保留本轮已发现的局部结果。":L"Incomplete scan; discovered entries retained.";}
                else {s.notice=winshun::wide(r->error);if(r->cancelled)s.notice=s.spec.zh?L"扫描已取消。":L"Scan cancelled.";}
            } else {
                s.completeResult=r;s.partialResult.reset();s.partialView=false;s.lastScanError.clear();
                adoptResult(s,r,false);s.notice=r->fallbackReason.empty()?L"":(s.spec.zh?L"本次使用普通扫描：":L"Standard scan used: ")+r->fallbackReason;saveConfigNow(s);
            }
            if(!s.tree&&s.lastScanError.empty())s.driveCards=listFixedDrives();
            computeLayout(s);relayoutView(s);::InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;


    case WM_CLOSE:
        s.cancelFlag = true;
        joinScanThread(s);
        {
            MSG stale;
            while (::PeekMessageW(&stale, hwnd, WM_APP_PROGRESS, WM_APP_PARTIAL, PM_REMOVE)) {
                if (stale.message == WM_APP_PROGRESS)
                    delete reinterpret_cast<ScanProgress*>(stale.lParam);
                else if (stale.message == WM_APP_DONE)
                    delete reinterpret_cast<ScanResult*>(stale.lParam);
                else if(stale.message==WM_APP_PARTIAL) delete reinterpret_cast<std::shared_ptr<ScanResult>*>(stale.lParam);
            }
        }
        ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

inline void threadMain(ViewerSpec spec)
{
    ViewerState& s = state();
    ensureDpiAwareness();
    s.spec = std::move(spec);
    s.palette = s.spec.theme == "light" ? lightPalette() : darkPalette();
    // The host's own colours, when it sent them (an older host, or a test
    // that runs the plugin on its own, sends none: the built-in palettes stay).
    s.palette = hostPalette(s.spec.appearance, s.palette);
    if (const HDC dc = ::GetDC(nullptr)) {
        s.dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
        ::ReleaseDC(nullptr, dc);
    }
    // The defaultView setting decides which view the window opens with; the
    // saved file keeps only the last scanned path.
    s.sunburst = s.spec.defaultSunburst;
    const ViewConfig cfg = loadViewConfig(s.spec.dataDir);
    s.lastPath = cfg.lastPath;
    s.lastScanError.clear();
    s.cancelled = false;
    s.cancelFlag = false;
    s.listFolders = true;
    s.history.clear();s.historyIndex=-1;s.selected=nullptr;s.selectedPath.clear();s.filter.clear();s.notice.clear();s.pendingPath.clear();
    s.completeResult.reset();s.partialResult.reset();s.partialView=false;s.resultIncomplete=false;
    s.depth=1;s.splitRatio=.58;s.editingAddress=false;s.mobileList=false;s.groupFilter.clear();s.rows.clear();
    s.keyboardFocus=false;s.focusIndex=-1;
    s.topScroll = 0;
    s.hoverDrive = s.hoverFolder = -1;
    s.hoverScan = s.hoverBack = s.hoverTreemapBtn = s.hoverSunburstBtn = false;
    s.driveCards = listFixedDrives();
    makeFonts(s);

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
    wc.style=CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"WinShunDiskViewWindow";
    ::RegisterClassExW(&wc);

    RECT wr {0, 0, S(1180, s), S(760, s)};
    ::AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    s.hwnd = ::CreateWindowExW(0, wc.lpszClassName, s.spec.zh ? L"磁盘透视 · Win顺" : L"Disk View · WinShun",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
                               wr.bottom - wr.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!s.hwnd) {
        std::lock_guard<std::mutex> lk(s.m);
        s.error = "无法创建窗口 / cannot create the window";
        s.running = false;
        s.cv.notify_all();
        return;
    }
    s.windowIcons.update(s.hwnd);
    computeLayout(s);

    s.edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, s.rcEdit.left,
                               s.rcEdit.top, s.rcEdit.right - s.rcEdit.left, s.rcEdit.bottom - s.rcEdit.top,
                               s.hwnd, nullptr, wc.hInstance, nullptr);
    s.origEditProc = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(s.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
    s.filterEdit=::CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,100,28,s.hwnd,reinterpret_cast<HMENU>(101),wc.hInstance,nullptr);
    s.origFilterProc=reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(s.filterEdit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(filterProc)));
    applyEditFont(s);::SendMessageW(s.filterEdit,WM_SETFONT,reinterpret_cast<WPARAM>(s.font),TRUE);
    computeLayout(s);

    const std::wstring seed = !s.spec.initialPath.empty() ? s.spec.initialPath : cfg.lastPath;
    ::SetWindowTextW(s.edit, seed.c_str());
    ::SendMessageW(s.edit, EM_SETSEL, seed.size(), seed.size());

    if (!s.spec.initialPath.empty())
        ::PostMessageW(s.hwnd,WM_APP_START_SCAN,0,0);

    ::ShowWindow(s.hwnd, SW_SHOW);
    // A launcher may hide the console via STARTUPINFO. Windows applies that
    // flag to the first ShowWindow call too; explicitly reveal our GUI after it.
    if(!::IsWindowVisible(s.hwnd))::ShowWindow(s.hwnd,SW_SHOWNORMAL);
    ::SetForegroundWindow(s.hwnd);

    {
        std::lock_guard<std::mutex> lk(s.m);
        s.visible = true;
    }
    s.cv.notify_all();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    s.windowIcons.clear();

    // Teardown: stop the scan first so the thread never posts into a dead
    // window, then release everything the GUI thread created.
    s.cancelFlag = true;
    joinScanThread(s);
    MSG stale;
    while (::PeekMessageW(&stale, s.hwnd, WM_APP_PROGRESS, WM_APP_PARTIAL, PM_REMOVE)) {
        if (stale.message == WM_APP_PROGRESS)
            delete reinterpret_cast<ScanProgress*>(stale.lParam);
        else if (stale.message == WM_APP_DONE)
            delete reinterpret_cast<ScanResult*>(stale.lParam);
                else if(stale.message==WM_APP_PARTIAL) delete reinterpret_cast<std::shared_ptr<ScanResult>*>(stale.lParam);
    }

    if (s.editBrush) {
        ::DeleteObject(s.editBrush);
        s.editBrush = nullptr;
    }
    if (s.editFont) {
        ::DeleteObject(s.editFont);
        s.editFont = nullptr;
    }
    if (s.font) {
        ::DeleteObject(s.font);
        s.font = nullptr;
    }
    if (s.fontBold) {
        ::DeleteObject(s.fontBold);
        s.fontBold = nullptr;
    }
    if (s.fontSmall) {
        ::DeleteObject(s.fontSmall);
        s.fontSmall = nullptr;
    }
    s.filterEdit=nullptr;s.origFilterProc=nullptr;s.completeResult.reset();s.partialResult.reset();s.rows.clear();s.groups.clear();
    s.edit = nullptr;
    if(s.fontHeading){::DeleteObject(s.fontHeading);s.fontHeading=nullptr;}
    s.origEditProc = nullptr;
    s.tree.reset();
    s.viewRoot = nullptr;
    s.drill.clear();
    s.tiles.clear();
    s.slices.clear();
    s.largest.clear();
    s.rootLargest.clear();
    s.folders.clear();
    s.skippedList.clear();
    s.crumbRects.clear();
    s.crumbNodes.clear();
    s.driveCards.clear();
    s.driveRects.clear();
    s.scanning = false;
    ::UnregisterClassW(L"WinShunDiskViewWindow", ::GetModuleHandleW(nullptr));

    {
        std::lock_guard<std::mutex> lk(s.m);
        s.hwnd = nullptr;
        s.visible = false;
        s.running = false;
    }
    s.cv.notify_all();
}

} // namespace detail

bool openViewerWindow(const ViewerSpec& spec, std::string& error)
{
    detail::ensureDpiAwareness();
    detail::ViewerState& s = detail::state();
    {
        std::lock_guard<std::mutex> lk(s.m);
        if (s.running && s.hwnd)
            ::PostMessageW(s.hwnd, WM_CLOSE, 0, 0); // one window at a time
    }
    closeViewerAndWait(1500);

    error.clear();
    {
        std::lock_guard<std::mutex> lk(s.m);
        if (s.running) { // the previous thread missed its timeout; refuse rather than race it
            error = "previous disk view window did not close in time";
            return false;
        }
        s.error.clear();
        s.visible = false;
        s.running = true;
        s.worker = std::thread(detail::threadMain, spec);
    }

    std::unique_lock<std::mutex> lk(s.m);
    const bool ok = s.cv.wait_for(lk, std::chrono::seconds(3), [&] { return s.visible || !s.error.empty(); });
    if (!ok) {
        error = "timed out waiting for the window thread";
        return false;
    }
    error = s.error;
    return s.visible;
}

bool viewerVisible()
{
    detail::ViewerState& s = detail::state();
    std::lock_guard<std::mutex> lk(s.m);
    return s.visible;
}

bool updateViewerAppearance(const winshun::Json& appearance)
{
    detail::ViewerState& s = detail::state();
    std::lock_guard<std::mutex> lk(s.m);
    if (!s.hwnd)
        return false;
    s.pendingAppearance = appearance;
    ::PostMessageW(s.hwnd, detail::WM_APP_APPEARANCE, 0, 0);
    return true;
}

void closeViewerAndWait(unsigned timeoutMs)
{
    detail::ViewerState& s = detail::state();
    {
        std::lock_guard<std::mutex> lk(s.m);
        if (s.hwnd)
            ::PostMessageW(s.hwnd, WM_CLOSE, 0, 0);
    }
    std::unique_lock<std::mutex> lk(s.m);
    const bool closed = s.cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return !s.running; });
    if (closed && s.worker.joinable()) {
        s.worker.join();
    } else if (!closed && s.worker.joinable()) {
        // The window did not close in time. Detach rather than join: a
        // joinable std::thread left in the static state would call
        // std::terminate() at program exit.
        s.worker.detach();
        winshun::debug("ra9fael.diskview: viewer close timed out; detached");
    }
}

} // namespace diskview

#endif // DISKVIEW_VIEWER_H
