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
#include "tree.h"
#include "winshun.h"

#include <windows.h>
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
    const COLORREF accent = RGB(88, 166, 255);
    const COLORREF panel = RGB(31, 34, 39);
    return {RGB(24, 26, 30), panel, RGB(232, 234, 237), RGB(232, 234, 237), RGB(120, 128, 136),
            accent, RGB(240, 128, 128), RGB(52, 57, 64), accent, accent,
            accent, tileText(accent), lighten(panel, 14), RGB(20, 22, 26), true};
}

inline Palette lightPalette()
{
    const COLORREF accent = RGB(9, 105, 218);
    const COLORREF panel = RGB(255, 255, 255);
    return {RGB(246, 247, 249), panel, RGB(24, 26, 30), RGB(24, 26, 30), RGB(110, 118, 126),
            accent, RGB(180, 40, 40), RGB(210, 214, 219), accent, accent,
            accent, tileText(accent), lighten(panel, -10), RGB(255, 255, 255), false};
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
    if (p.dark)
        return hsl(215, 32, std::max(26, 52 - depth * 5));
    return hsl(215, 32, std::min(88, 74 + depth * 3));
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
        {L"mp4 mkv avi mov wmv flv webm mp3 wav flac ogg m4a", 20},
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
    if (p.dark)
        return hsl(hue, 38, std::max(24, 44 - depth * 2));
    return hsl(hue, 42, std::min(86, 70 + depth * 2));
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
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr;

    // The host's new look, waited for by the window thread (under `m`).
    winshun::Json pendingAppearance;

    std::shared_ptr<Node> tree; // the last completed scan (immutable once set)
    bool scanning = false;
    bool cancelled = false;
    std::atomic<bool> cancelFlag {false};
    std::thread scanThread;
    ScanProgress progress;
    std::string lastScanError; // UTF-8, shown in the content area

    std::vector<LargeFile> largest;
    std::vector<SkippedDir> skippedList;
    std::uint64_t files = 0, dirs = 0, skippedDirs = 0, skippedLinks = 0;
    std::uint64_t elapsedMs = 0;
    std::wstring lastPath; // the display path of the last started scan

    bool sunburst = false;
    const Node* viewRoot = nullptr; // into *tree
    std::vector<const Node*> drill; // ancestors above viewRoot
    std::vector<Tile> tiles;
    std::vector<Slice> slices;

    int hoverTile = -1, hoverSlice = -1, hoverTop = -1, hoverCrumb = -1;
    bool hoverScan = false, hoverBack = false, hoverTreemapBtn = false, hoverSunburstBtn = false;

    RECT rcClient = {}, rcToolbar = {}, rcContent = {}, rcChart = {}, rcTopList = {}, rcStatus = {};
    RECT rcEdit = {}, rcScan = {}, rcBack = {}, rcTreemapBtn = {}, rcSunburstBtn = {};
    std::vector<RECT> crumbRects;
    std::vector<const Node*> crumbNodes;
    int topScroll = 0;

    HWND edit = nullptr;
    WNDPROC origEditProc = nullptr;
    HFONT editFont = nullptr;
    HBRUSH editBrush = nullptr;

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
    if (base.empty())
        return name;
    if (base.back() == L'\\')
        return base + name;
    return base + L"\\" + name;
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

inline HFONT makeOneFont(int pt, bool bold, int dpi)
{
    const int px = -MulDiv(pt, dpi, 72);
    return ::CreateFontW(px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

inline void applyEditFont(ViewerState& s)
{
    if (!s.edit)
        return;
    if (s.editFont)
        ::DeleteObject(s.editFont);
    s.editFont = makeOneFont(10, false, s.dpi);
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
    s.font = makeOneFont(9, false, s.dpi);
    s.fontBold = makeOneFont(9, true, s.dpi);
    s.fontSmall = makeOneFont(8, false, s.dpi);
    applyEditFont(s);
}

inline int maxRows(const ViewerState& s)
{
    const int h = s.rcTopList.bottom - s.rcTopList.top - s.topHeaderH;
    return h > 0 ? h / s.rowH : 0;
}

inline void computeLayout(ViewerState& s)
{
    ::GetClientRect(s.hwnd, &s.rcClient);
    const int w = s.rcClient.right, h = s.rcClient.bottom;
    s.toolbarH = S(40, s);
    s.statusH = S(22, s);
    s.topPaneW = S(300, s);
    s.rowH = S(22, s);
    s.topHeaderH = S(28, s);
    s.rcToolbar = {0, 0, w, s.toolbarH};
    s.rcStatus = {0, h - 2 * s.statusH, w, h};
    s.rcContent = {0, s.toolbarH, w, h - 2 * s.statusH};

    int x = S(8, s);
    const int btnY = S(7, s), btnH = S(26, s);
    s.rcScan = {0, btnY, 0, btnY + btnH};
    s.rcScan.left = w - S(8, s) - S(72, s);
    s.rcScan.right = w - S(8, s);
    s.rcTreemapBtn = {0, btnY, 0, btnY + btnH};
    s.rcTreemapBtn.right = s.rcScan.left - S(8, s);
    s.rcTreemapBtn.left = s.rcTreemapBtn.right - S(72, s);
    s.rcSunburstBtn = s.rcTreemapBtn;
    s.rcSunburstBtn.right = s.rcTreemapBtn.left - S(4, s);
    s.rcSunburstBtn.left = s.rcSunburstBtn.right - S(80, s);
    s.rcBack = s.rcSunburstBtn;
    s.rcBack.right = s.rcSunburstBtn.left - S(8, s);
    s.rcBack.left = s.rcBack.right - S(56, s);
    s.rcEdit = {x, btnY, std::max(x + S(120, s), static_cast<int>(s.rcBack.left) - S(8, s)), btnY + btnH};
    if (s.edit)
        ::MoveWindow(s.edit, s.rcEdit.left, s.rcEdit.top, s.rcEdit.right - s.rcEdit.left,
                     s.rcEdit.bottom - s.rcEdit.top, TRUE);

    const bool hasTop = s.tree && !s.largest.empty();
    if (hasTop) {
        s.rcTopList = {w - s.topPaneW, s.rcContent.top, w, s.rcContent.bottom};
        s.rcChart = {s.rcContent.left, s.rcContent.top, s.rcTopList.left, s.rcContent.bottom};
    } else {
        s.rcTopList = {};
        s.rcChart = s.rcContent;
    }
}

// Re-runs the active layout for the current view root and chart rect.
inline void relayoutView(ViewerState& s)
{
    s.tiles.clear();
    s.slices.clear();
    s.hoverTile = s.hoverSlice = s.hoverTop = s.hoverCrumb = -1;
    if (!s.tree || !s.viewRoot)
        return;
    const std::wstring base = nodePath(s, s.viewRoot);
    if (!s.sunburst) {
        const double dpiF = static_cast<double>(s.dpi) / 96.0;
        const double minArea = 24.0 * dpiF * dpiF;
        const Rect area {static_cast<double>(s.rcChart.left), static_cast<double>(s.rcChart.top),
                         static_cast<double>(s.rcChart.right - s.rcChart.left),
                         static_cast<double>(s.rcChart.bottom - s.rcChart.top)};
        treemapTiles(*s.viewRoot, area, 0, minArea, s.tiles, base);
    } else {
        const double cx = (s.rcChart.left + s.rcChart.right) / 2.0;
        const double cy = (s.rcChart.top + s.rcChart.bottom) / 2.0;
        const double rMax =
            std::min(s.rcChart.right - s.rcChart.left, s.rcChart.bottom - s.rcChart.top) / 2.0 - S(12, s);
        const double ringW = S(34, s);
        const double r0 = std::min<double>(S(84, s),
                                           std::max(rMax - 2 * ringW, static_cast<double>(S(24, s))));
        if (rMax > r0)
            sunburstSlices(*s.viewRoot, cx, cy, r0, ringW, -3.14159265358979323846 / 2,
                           -3.14159265358979323846 / 2 + 6.28318530717958647692, 0, 0.003, s.slices, base);
    }
}

// ---- scanning ----------------------------------------------------------------------

inline void joinScanThread(ViewerState& s)
{
    if (s.scanThread.joinable())
        s.scanThread.join();
}

inline void scanThreadMain(HWND hwnd, std::wstring prefixedPath)
{
    ViewerState& s = state();
    auto* result = new ScanResult();
    scanPath(prefixedPath,
             [hwnd](const ScanProgress& p) {
                 auto* copy = new ScanProgress(p);
                 if (!::PostMessageW(hwnd, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(copy)))
                     delete copy;
                 return true;
             },
             s.cancelFlag, result);
    if (!::PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result)))
        delete result;
}

inline void startScan(ViewerState& s, const std::wstring& rawPath)
{
    std::wstring prefixed;
    const std::wstring display = normalizePath(rawPath, &prefixed);
    if (display.empty()) {
        s.lastScanError = s.spec.zh ? "路径为空，请先输入一个要扫描的路径。"
                                    : "The path is empty; type a path to scan first.";
        ::InvalidateRect(s.hwnd, nullptr, FALSE);
        return;
    }
    if (s.scanning)
        return;
    s.scanning = true;
    s.cancelled = false;
    s.lastScanError.clear();
    s.cancelFlag = false;
    s.progress = ScanProgress {};
    s.lastPath = display;
    joinScanThread(s);
    s.scanThread = std::thread(scanThreadMain, s.hwnd, prefixed);
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
    fillWith(hdc, r, active ? s.palette.accentFill
                            : hover ? s.palette.hover : s.palette.panel);
    frameWith(hdc, r, active ? s.palette.accentFill : s.palette.border);
    const COLORREF text = active ? s.palette.accentText : s.palette.text;
    RECT tr = r;
    ::OffsetRect(&tr, 0, 1);
    drawTextIn(hdc, label, tr, text, DT_CENTER | DT_VCENTER);
}

// The hover status line for one node: kind, size, counts, unreadable entries.
inline std::wstring describe(const ViewerState& s, const Node* n)
{
    const bool zh = s.spec.zh;
    std::wstring t = n->dir ? (zh ? L"文件夹 · " : L"folder · ") : (zh ? L"文件 · " : L"file · ");
    t += winshun::wide(formatBytes(n->logical, zh));
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
        if (static_cast<int>(i) == s.hoverTile)
            fill = lighten(fill, s.palette.dark ? 18 : -14);
        fillWith(hdc, r, fill);
        frameWith(hdc, r, s.palette.bg);
        RECT rr = r;
        ::InflateRect(&rr, -S(4, s), -S(2, s));
        const int w = rr.right - rr.left, h = rr.bottom - rr.top;
        if (w >= S(48, s) && h >= S(15, s)) {
            const COLORREF text = tileText(fill);
            drawTextIn(hdc, t.node->name, rr, text, DT_LEFT | DT_END_ELLIPSIS);
            if (h >= S(28, s)) {
                RECT sr = rr;
                sr.top += S(14, s);
                const COLORREF sub = s.palette.dark ? lighten(text, -70) : lighten(text, 70);
                drawTextIn(hdc, winshun::wide(formatBytes(t.node->logical, s.spec.zh)), sr, sub,
                           DT_LEFT | DT_END_ELLIPSIS);
            }
        }
        if (static_cast<int>(i) == s.hoverTile) {
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
        if (static_cast<int>(i) == s.hoverSlice)
            fill = lighten(fill, s.palette.dark ? 22 : -18);
        ::SetDCBrushColor(hdc, fill);
        ::SetDCPenColor(hdc, s.palette.bg);
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
    const RECT hole {static_cast<LONG>(cx - S(80, s)), static_cast<LONG>(cy - S(26, s)),
                     static_cast<LONG>(cx + S(80, s)), static_cast<LONG>(cy + S(26, s))};
    RECT nr = hole;
    nr.bottom = static_cast<LONG>(cy);
    RECT tr = hole;
    tr.top = static_cast<LONG>(cy);
    HGDIOBJ oldFont = ::SelectObject(hdc, s.fontBold);
    drawTextIn(hdc, name, nr, s.palette.text, DT_CENTER | DT_BOTTOM | DT_END_ELLIPSIS);
    ::SelectObject(hdc, oldFont);
    drawTextIn(hdc, total, tr, s.palette.muted, DT_CENTER | DT_TOP | DT_END_ELLIPSIS);
}

inline void drawTopList(ViewerState& s, HDC hdc)
{
    fillWith(hdc, s.rcTopList, s.palette.panel);
    frameWith(hdc, s.rcTopList, s.palette.border);
    RECT hr = s.rcTopList;
    hr.bottom = hr.top + s.topHeaderH;
    fillWith(hdc, hr, lighten(s.palette.panel, s.palette.dark ? 8 : -8));
    const std::wstring title = (s.spec.zh ? L"最大的文件 (" : L"Largest files (")
                             + winshun::wide(formatCount(s.largest.size())) + L")";
    RECT tr = hr;
    ::InflateRect(&tr, -S(10, s), 0);
    drawTextIn(hdc, title, tr, s.palette.muted, DT_LEFT | DT_VCENTER);

    const int rows = maxRows(s);
    const int maxScroll = std::max(0, static_cast<int>(s.largest.size()) - rows);
    s.topScroll = std::clamp(s.topScroll, 0, maxScroll);
    for (int i = 0; i < rows; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i + s.topScroll);
        if (idx >= s.largest.size())
            break;
        const LargeFile& f = s.largest[idx];
        RECT rr = s.rcTopList;
        rr.top = s.rcTopList.top + s.topHeaderH + i * s.rowH;
        rr.bottom = rr.top + s.rowH;
        rr.left += S(8, s);
        rr.right -= S(8, s);
        if (static_cast<int>(idx) == s.hoverTop)
            fillWith(hdc, rr, s.palette.hover);
        RECT sw = rr;
        sw.right = sw.left + S(10, s);
        sw.top += (s.rowH - S(10, s)) / 2;
        sw.bottom = sw.top + S(10, s);
        fillWith(hdc, sw, fileColor(fileNameOf(f.path), 0, s.palette));
        const std::wstring sizeText = winshun::wide(formatBytes(f.logical, s.spec.zh));
        SIZE sz {};
        ::GetTextExtentPoint32W(hdc, sizeText.c_str(), static_cast<int>(sizeText.size()), &sz);
        RECT sizeR = rr;
        sizeR.left = std::max(sizeR.left + S(16, s), sizeR.right - sz.cx);
        drawTextIn(hdc, sizeText, sizeR, s.palette.muted, DT_RIGHT | DT_VCENTER);
        RECT nameR = rr;
        nameR.left = sw.right + S(6, s);
        nameR.right = std::max(nameR.left, sizeR.left - S(8, s));
        drawTextIn(hdc, fileNameOf(f.path), nameR, s.palette.text, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

inline void drawStatusRows(ViewerState& s, HDC hdc)
{
    const int top = s.rcStatus.top;
    RECT row1 = {0, top, s.rcClient.right, top + s.statusH};
    RECT row2 = {0, top + s.statusH, s.rcClient.right, s.rcStatus.bottom};
    frameWith(hdc, {0, top, s.rcClient.right, top + 1}, s.palette.border);
    fillWith(hdc, row1, s.palette.panel);
    fillWith(hdc, row2, s.palette.panel);

    // Breadcrumb: the scan root, then every drilled level. Segments are
    // clickable; their rects are rebuilt here on every frame.
    s.crumbRects.clear();
    s.crumbNodes.clear();
    int x = S(8, s);
    const auto addSeg = [&](const Node* n, const std::wstring& label) {
        if (x > row1.right - S(160, s))
            return;
        SIZE sz {};
        ::GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &sz);
        RECT r = {x, row1.top, x + sz.cx + S(4, s), row1.bottom};
        const bool hover = static_cast<int>(s.crumbRects.size()) == s.hoverCrumb;
        drawTextIn(hdc, label, r, hover ? s.palette.accent : s.palette.text, DT_LEFT | DT_VCENTER);
        s.crumbRects.push_back(r);
        s.crumbNodes.push_back(n);
        x = r.right;
    };
    if (s.tree) {
        addSeg(s.tree.get(), s.tree->name);
        for (const Node* d : s.drill)
            addSeg(d, L"\\" + d->name);
        if (s.viewRoot && s.viewRoot != s.tree.get())
            addSeg(s.viewRoot, L"\\" + s.viewRoot->name);
    }

    // Hover info: whatever the pointer is over, else the current view root.
    const Node* infoNode = s.viewRoot;
    if (s.hoverTile >= 0 && s.hoverTile < static_cast<int>(s.tiles.size()))
        infoNode = s.tiles[static_cast<std::size_t>(s.hoverTile)].node;
    else if (s.hoverSlice >= 0 && s.hoverSlice < static_cast<int>(s.slices.size()))
        infoNode = s.slices[static_cast<std::size_t>(s.hoverSlice)].node;
    RECT ir = row1;
    ir.right -= S(8, s);
    if (infoNode)
        drawTextIn(hdc, describe(s, infoNode), ir, s.palette.muted, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);

    // Row 2: what was scanned; on the right, progress or totals.
    const bool zh = s.spec.zh;
    std::wstring left;
    if (s.tree) {
        left = zh ? L"已扫 " : L"scanned ";
        left += winshun::wide(formatCount(s.files)) + (zh ? L" 个文件 · " : L" files · ");
        left += winshun::wide(formatCount(s.dirs)) + (zh ? L" 个文件夹" : L" folders");
        if (s.skippedDirs > 0)
            left += (zh ? L" · 跳过 " : L" · skipped ") + winshun::wide(formatCount(s.skippedDirs))
                  + (zh ? L" 个目录" : L" dirs");
        if (s.skippedLinks > 0)
            left += (zh ? L" · " : L" · ") + winshun::wide(formatCount(s.skippedLinks))
                  + (zh ? L" 条链接未跟随" : L" links not followed");
        left += (zh ? L" · 耗时 " : L" · took ") + std::to_wstring(s.elapsedMs) + L" ms";
    } else {
        left = zh ? L"输入一个路径（如 C:\\）后回车开始扫描" : L"Type a path (e.g. C:\\) and press Enter to scan";
    }
    RECT lr = row2;
    ::InflateRect(&lr, -S(8, s), 0);
    drawTextIn(hdc, left, lr, s.palette.muted, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    std::wstring right;
    COLORREF rightColor = s.palette.muted;
    if (s.scanning) {
        right = (zh ? L"正在扫描… " : L"scanning… ") + winshun::wide(formatBytes(s.progress.bytes, zh))
              + (zh ? L" · " : L" · ") + winshun::wide(formatCount(s.progress.files))
              + (zh ? L" 个文件" : L" files");
        rightColor = s.palette.accent;
    } else if (s.cancelled) {
        right = zh ? L"已取消 · 显示的是上次完成的数据" : L"cancelled · showing the last completed scan";
        rightColor = s.palette.danger;
    } else if (s.tree) {
        right = (zh ? L"逻辑 " : L"logical ") + winshun::wide(formatBytes(s.tree->logical, zh))
              + (zh ? L" · 按簇近似 " : L" · ~on disk ") + winshun::wide(formatBytes(s.tree->allocApprox, zh));
    }
    RECT rr2 = row2;
    rr2.right -= S(8, s);
    drawTextIn(hdc, right, rr2, rightColor, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
}

inline void drawProgressBox(ViewerState& s, HDC hdc)
{
    const int w = S(430, s), h = S(96, s);
    RECT box = {(s.rcChart.left + s.rcChart.right - w) / 2, (s.rcChart.top + s.rcChart.bottom - h) / 2, 0, 0};
    box.right = box.left + w;
    box.bottom = box.top + h;
    fillWith(hdc, box, s.palette.panel);
    frameWith(hdc, box, s.palette.accent);
    RECT tr = box;
    ::InflateRect(&tr, -S(16, s), -S(10, s));
    const bool zh = s.spec.zh;
    drawTextIn(hdc, zh ? L"正在扫描…" : L"Scanning…", tr, s.palette.text, DT_LEFT);
    RECT line = tr;
    line.top += S(22, s);
    const std::wstring l2 = winshun::wide(formatBytes(s.progress.bytes, zh)) + (zh ? L" · " : L" · ")
                          + winshun::wide(formatCount(s.progress.files)) + (zh ? L" 个文件 · " : L" files · ")
                          + winshun::wide(formatCount(s.progress.dirs)) + (zh ? L" 个文件夹" : L" folders");
    drawTextIn(hdc, l2, line, s.palette.muted, DT_LEFT);
    RECT cur = tr;
    cur.top += S(44, s);
    cur.bottom = box.bottom - S(10, s);
    drawTextIn(hdc, s.progress.current, cur, s.palette.muted, DT_LEFT | DT_END_ELLIPSIS);
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

    const wchar_t* lines[] = {
        zh ? L"在上方输入一个路径（如 C:\\ 或 D:\\projects），回车开始扫描。"
           : L"Type a path above (e.g. C:\\ or D:\\projects) and press Enter to scan.",
        zh ? L"扫描只读取目录条目，不打开任何文件；junction 与符号链接只计数、不跟随。"
           : L"Scanning reads directory entries only; junctions and symlinks are counted, never followed.",
        zh ? L"方块图：面积≈占用大小，点击方块进入文件夹，Backspace 返回上级，F5 重新扫描，Ctrl+V 切换视图。"
           : L"Treemap: area ≈ size. Click a tile to enter a folder, Backspace goes up, F5 rescans, Ctrl+V switches views.",
        zh ? L"无法读取的目录会计入统计，不会被当作空目录。"
           : L"Unreadable directories are counted and listed; they are never treated as empty.",
    };
    int y = r.top + S(48, s);
    for (const wchar_t* line : lines) {
        RECT lr = r;
        lr.top = y;
        lr.bottom = y + S(20, s);
        drawTextIn(hdc, line, lr, s.palette.muted, DT_LEFT);
        y += S(24, s);
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

inline void paintAll(ViewerState& s, HDC hdc, const RECT& client)
{
    HGDIOBJ oldFont = ::SelectObject(hdc, s.font);
    fillWith(hdc, client, s.palette.bg);

    // toolbar
    fillWith(hdc, s.rcToolbar, s.palette.panel);
    frameWith(hdc, {s.rcToolbar.left, s.rcToolbar.bottom - 1, s.rcToolbar.right, s.rcToolbar.bottom},
              s.palette.border);
    drawButton(s, hdc, s.rcScan, s.scanning ? (s.spec.zh ? L"停止" : L"Stop") : (s.spec.zh ? L"扫描" : L"Scan"),
               s.scanning, s.hoverScan);
    drawButton(s, hdc, s.rcBack, s.spec.zh ? L"上级" : L"Up", false, s.hoverBack);
    drawButton(s, hdc, s.rcTreemapBtn, s.spec.zh ? L"方块" : L"Treemap", !s.sunburst,
               s.hoverTreemapBtn && s.sunburst);
    drawButton(s, hdc, s.rcSunburstBtn, s.spec.zh ? L"圆盘" : L"Sunburst", s.sunburst,
               s.hoverSunburstBtn && !s.sunburst);

    // chart
    if (s.tree) {
        if (!s.sunburst && !s.tiles.empty())
            drawTiles(s, hdc);
        else if (s.sunburst && !s.slices.empty())
            drawSlices(s, hdc);
        else {
            RECT r = s.rcChart;
            ::InflateRect(&r, -S(24, s), -S(24, s));
            drawTextIn(hdc, s.spec.zh ? L"这个文件夹为空（或所有条目都太小，无法画出）。"
                                      : L"This folder is empty (or every entry is too small to draw).",
                       r, s.palette.muted, DT_LEFT);
        }
    } else if (s.scanning) {
        drawProgressBox(s, hdc);
    } else if (!s.lastScanError.empty()) {
        drawScanError(s, hdc);
    } else {
        drawWelcome(s, hdc);
    }

    if (s.rcTopList.right > s.rcTopList.left)
        drawTopList(s, hdc);

    if (s.scanning && s.tree)
        drawProgressBox(s, hdc);

    drawStatusRows(s, hdc);
    ::SelectObject(hdc, oldFont);
}

// ---- hit testing --------------------------------------------------------------------

inline int hitTile(const ViewerState& s, int x, int y)
{
    // Children are appended after their parent, so the reverse scan finds the
    // innermost (topmost) tile first.
    for (std::size_t i = s.tiles.size(); i-- > 0;)
        if (s.tiles[i].rect.contains(static_cast<double>(x), static_cast<double>(y)))
            return static_cast<int>(i);
    return -1;
}

inline int hitSlice(const ViewerState& s, int x, int y)
{
    if (s.slices.empty())
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
    return idx >= 0 && idx < static_cast<int>(s.largest.size()) ? idx : -1;
}

inline int hitCrumb(const ViewerState& s, int x, int y)
{
    for (std::size_t i = 0; i < s.crumbRects.size(); ++i)
        if (inRect(s.crumbRects[i], x, y))
            return static_cast<int>(i);
    return -1;
}

inline bool anyClickable(const ViewerState& s, int x, int y)
{
    return inRect(s.rcScan, x, y) || inRect(s.rcBack, x, y) || inRect(s.rcTreemapBtn, x, y)
           || inRect(s.rcSunburstBtn, x, y) || hitTop(s, x, y) >= 0 || hitCrumb(s, x, y) >= 0
           || hitTile(s, x, y) >= 0 || hitSlice(s, x, y) >= 0;
}

// ---- navigation ---------------------------------------------------------------------

// Walks from the scan root along `path` (the display form). `chain` receives
// every descended node, root excluded. Returns nullptr when the path does not
// exist in the tree.
inline const Node* descend(ViewerState& s, const std::wstring& path, std::vector<const Node*>* chain)
{
    if (!s.tree)
        return nullptr;
    if (path == s.tree->name)
        return s.tree.get();
    if (path.rfind(s.tree->name + L"\\", 0) != 0)
        return nullptr;
    const Node* n = s.tree.get();
    std::size_t pos = s.tree->name.size() + 1;
    for (;;) {
        const std::size_t slash = path.find(L'\\', pos);
        const std::size_t end = slash == std::wstring::npos ? path.size() : slash;
        n = findChild(*n, path.substr(pos, end - pos));
        if (!n)
            return nullptr;
        if (chain)
            chain->push_back(n);
        if (slash == std::wstring::npos)
            return n;
        pos = slash + 1;
    }
}

inline void drillDown(ViewerState& s, const Node* child)
{
    if (!child || !child->dir || !s.viewRoot)
        return;
    s.drill.push_back(s.viewRoot);
    s.viewRoot = child;
    relayoutView(s);
    ::InvalidateRect(s.hwnd, nullptr, FALSE);
}

inline void goUp(ViewerState& s)
{
    if (s.drill.empty())
        return;
    s.viewRoot = s.drill.back();
    s.drill.pop_back();
    relayoutView(s);
    ::InvalidateRect(s.hwnd, nullptr, FALSE);
}

inline void drillTo(ViewerState& s, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(s.crumbNodes.size()))
        return;
    s.viewRoot = s.crumbNodes[static_cast<std::size_t>(idx)];
    if (idx == 0)
        s.drill.clear();
    else
        s.drill.assign(s.crumbNodes.begin() + 1, s.crumbNodes.begin() + idx);
    relayoutView(s);
    ::InvalidateRect(s.hwnd, nullptr, FALSE);
}

inline void setViewMode(ViewerState& s, bool sunburst)
{
    if (s.sunburst == sunburst)
        return;
    s.sunburst = sunburst;
    saveConfigNow(s);
    relayoutView(s);
    ::InvalidateRect(s.hwnd, nullptr, FALSE);
}

inline void updateHover(ViewerState& s, int x, int y)
{
    const int t = s.sunburst ? -1 : hitTile(s, x, y);
    const int sl = s.sunburst ? hitSlice(s, x, y) : -1;
    const int top = hitTop(s, x, y);
    const int crumb = hitCrumb(s, x, y);
    const bool scan = inRect(s.rcScan, x, y);
    const bool back = inRect(s.rcBack, x, y);
    const bool tm = inRect(s.rcTreemapBtn, x, y);
    const bool sb = inRect(s.rcSunburstBtn, x, y);
    if (t != s.hoverTile || sl != s.hoverSlice || top != s.hoverTop || crumb != s.hoverCrumb
        || scan != s.hoverScan || back != s.hoverBack || tm != s.hoverTreemapBtn || sb != s.hoverSunburstBtn) {
        s.hoverTile = t;
        s.hoverSlice = sl;
        s.hoverTop = top;
        s.hoverCrumb = crumb;
        s.hoverScan = scan;
        s.hoverBack = back;
        s.hoverTreemapBtn = tm;
        s.hoverSunburstBtn = sb;
        ::InvalidateRect(s.hwnd, nullptr, FALSE);
    }
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

inline LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ViewerState& s = state();
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        if (wp == VK_RETURN)
            startScan(s, editText(hwnd));
        else
            ::PostMessageW(s.hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    return ::CallWindowProcW(s.origEditProc, hwnd, msg, wp, lp);
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
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme {sizeof tme, TME_LEAVE, hwnd, 0};
        ::TrackMouseEvent(&tme);
        updateHover(s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    }
    case WM_MOUSELEAVE:
        s.hoverTile = s.hoverSlice = s.hoverTop = s.hoverCrumb = -1;
        s.hoverScan = s.hoverBack = s.hoverTreemapBtn = s.hoverSunburstBtn = false;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (inRect(s.rcScan, x, y)) {
            if (s.scanning)
                s.cancelFlag = true; // the DONE message will finish the state change
            else
                startScan(s, editText(s.edit));
            return 0;
        }
        if (inRect(s.rcBack, x, y)) {
            goUp(s);
            return 0;
        }
        if (inRect(s.rcTreemapBtn, x, y)) {
            setViewMode(s, false);
            return 0;
        }
        if (inRect(s.rcSunburstBtn, x, y)) {
            setViewMode(s, true);
            return 0;
        }
        const int c = hitCrumb(s, x, y);
        if (c >= 0) {
            drillTo(s, c);
            return 0;
        }
        if (!s.tree)
            return 0;
        const int t = s.sunburst ? hitSlice(s, x, y) : hitTile(s, x, y);
        if (t >= 0) {
            const Node* n = s.sunburst ? s.slices[static_cast<std::size_t>(t)].node
                                       : s.tiles[static_cast<std::size_t>(t)].node;
            if (n && n->dir)
                drillDown(s, n);
            return 0;
        }
        const int topIdx = hitTop(s, x, y);
        if (topIdx >= 0) {
            // A file row: show the folder that contains it.
            const std::wstring& p = s.largest[static_cast<std::size_t>(topIdx)].path;
            const std::size_t slash = p.find_last_of(L'\\');
            if (slash != std::wstring::npos) {
                std::vector<const Node*> chain;
                if (const Node* folder = descend(s, p.substr(0, slash), &chain)) {
                    s.drill.assign(chain.begin(), chain.end() - 1);
                    s.viewRoot = folder;
                    relayoutView(s);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (s.rcTopList.right <= s.rcTopList.left)
            return 0;
        POINT p {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ::ScreenToClient(hwnd, &p);
        if (p.x < s.rcTopList.left)
            return 0;
        const int rows = maxRows(s);
        const int maxScroll = std::max(0, static_cast<int>(s.largest.size()) - rows);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        s.topScroll = std::clamp(s.topScroll - (delta / WHEEL_DELTA) * 3, 0, maxScroll);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_BACK) {
            goUp(s);
            return 0;
        }
        if (wp == VK_F5) {
            if (!s.scanning)
                startScan(s, s.lastPath);
            return 0;
        }
        if (wp == 'V' && (::GetKeyState(VK_CONTROL) & 0x8000)) {
            setViewMode(s, !s.sunburst);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            const DWORD pos = ::GetMessagePos();
            POINT p {GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
            ::ScreenToClient(hwnd, &p);
            ::SetCursor(anyClickable(s, p.x, p.y) ? ::LoadCursorW(nullptr, IDC_HAND)
                                                  : ::LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == s.edit) {
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
        makeFonts(s);
        const RECT* sug = reinterpret_cast<const RECT*>(lp);
        ::SetWindowPos(hwnd, nullptr, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {S(680, s), S(460, s)};
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
    case WM_APP_PROGRESS:
        if (lp) {
            s.progress = *reinterpret_cast<ScanProgress*>(lp);
            delete reinterpret_cast<ScanProgress*>(lp);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_APP_DONE:
        joinScanThread(s);
        if (lp) {
            auto* r = reinterpret_cast<ScanResult*>(lp);
            s.scanning = false;
            s.cancelled = r->cancelled;
            // The breadcrumb may point into the old tree; drop it before that
            // tree can be replaced.
            s.crumbRects.clear();
            s.crumbNodes.clear();
            s.hoverTile = s.hoverSlice = s.hoverTop = s.hoverCrumb = -1;
            if (!r->error.empty()) {
                s.lastScanError = r->error; // the previous tree stays on display
            } else if (r->cancelled) {
                s.lastScanError.clear(); // partial data would misreport; keep the old scan
            } else {
                s.tree = std::make_shared<Node>(std::move(r->root));
                s.largest = std::move(r->largest);
                s.skippedList = std::move(r->skippedList);
                s.files = r->files;
                s.dirs = r->dirs;
                s.skippedDirs = r->skippedDirs;
                s.skippedLinks = r->skippedLinks;
                s.elapsedMs = r->elapsedMs;
                s.lastScanError.clear();
                s.viewRoot = s.tree.get();
                s.drill.clear();
                s.topScroll = 0;
                saveConfigNow(s);
            }
            delete r;
            computeLayout(s);
            relayoutView(s);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
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
    makeFonts(s);

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof wc;
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
    computeLayout(s);

    s.edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, s.rcEdit.left,
                               s.rcEdit.top, s.rcEdit.right - s.rcEdit.left, s.rcEdit.bottom - s.rcEdit.top,
                               s.hwnd, nullptr, wc.hInstance, nullptr);
    s.origEditProc = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(s.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
    applyEditFont(s);

    const std::wstring seed = !s.spec.initialPath.empty() ? s.spec.initialPath : cfg.lastPath;
    ::SetWindowTextW(s.edit, seed.c_str());
    ::SendMessageW(s.edit, EM_SETSEL, seed.size(), seed.size());

    if (!s.spec.initialPath.empty())
        startScan(s, s.spec.initialPath);

    ::ShowWindow(s.hwnd, SW_SHOW);
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

    // Teardown: stop the scan first so the thread never posts into a dead
    // window, then release everything the GUI thread created.
    s.cancelFlag = true;
    joinScanThread(s);
    MSG stale;
    while (::PeekMessageW(&stale, s.hwnd, WM_APP_PROGRESS, WM_APP_DONE, PM_REMOVE))
        delete reinterpret_cast<void*>(stale.lParam);

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
    s.edit = nullptr;
    s.origEditProc = nullptr;
    s.tree.reset();
    s.viewRoot = nullptr;
    s.drill.clear();
    s.tiles.clear();
    s.slices.clear();
    s.largest.clear();
    s.skippedList.clear();
    s.crumbRects.clear();
    s.crumbNodes.clear();
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
