// main.cpp - WinShun resident plugin: Disk View (磁盘透视).
//
// The SDK owns stdio and the JSON-RPC framing. This file only answers the
// `disk` query with entry rows and opens the viewer window on the "open"
// action. Queries never scan: the scan runs inside the window, started by the
// user, and touches nothing outside it (no deletes, no launches, no clipboard).
//
// Code and comments are English; the user-visible strings are localized with
// the host's locale (Chinese by default, English when locale starts with "en").
#include "winshun.h"

#include "viewer.h"

#include <memory>
#include <string>
#include <utility>

namespace {

std::string tr(bool zh, const char* chinese, const char* english)
{
    return zh ? chinese : english;
}

// The default view from settings; anything but "sunburst" is the treemap.
bool defaultSunburst(const winshun::Json& s)
{
    const winshun::Json& v = s["defaultView"];
    return v.isString() && v.toString() == "sunburst";
}

// The empty query gets one entry row that opens the window with the last
// scanned path seeded (not auto-scanned), plus a usage hint below it.
winshun::Items emptyItems(bool zh, const std::wstring& lastPath)
{
    std::string title = tr(zh, "打开磁盘透视", "Open Disk View");
    std::string subtitle = tr(zh, "Enter 打开窗口 · 输入 disk <路径> 可直接开始扫描，如 disk D:\\工作",
                              "Enter opens the window · type disk <path> to scan right away, e.g. disk D:\\work");
    if (!lastPath.empty()) {
        subtitle += tr(zh, " · 上次：", " · last: ") + winshun::utf8(lastPath);
        title += tr(zh, " · 上次扫描 ", " · last scan ") + winshun::utf8(lastPath);
    }
    winshun::Item item("disk.open", title, subtitle);
    item.withIcon("color:#0F766E")
        .withScore(100)
        .withData(winshun::Json(winshun::Json::Object {
            {"kind", "open"},
            {"path", ""},
        }));
    winshun::Items items;
    items.push_back(std::move(item));

    const std::string usage = "disk C:\\";
    winshun::Item help("disk.help", usage,
                       tr(zh, "回车把示例填入输入框 · 窗口里改路径、按 F5 随时重扫",
                          "Enter fills the example · change the path in the window and press F5 to rescan"));
    help.invalid()
        .withIcon("color:#185FA5")
        .withScore(90)
        .withCopy(usage)
        .withAutocomplete(usage)
        .withPreview(usage + "\n\n"
                     + tr(zh, "方块图（递归矩形）与圆盘（递归放射）共用同一份扫描数据，"
                              "点击进入文件夹，Backspace 返回上级。",
                              "The treemap and the sunburst share one scan; click to enter a folder, "
                              "Backspace goes up."));
    items.push_back(std::move(help));
    return items;
}

// A path in the query text: one row that opens and scans right away, plus a
// row with an example path.
winshun::Items pathItems(const std::string& text, bool zh)
{
    winshun::Item item("disk.open", tr(zh, "打开磁盘透视 · ", "Open Disk View · ") + text,
                       tr(zh, "Enter 打开窗口并扫描此路径", "Enter opens the window and scans this path"));
    item.withIcon("color:#0F766E")
        .withScore(100)
        .withData(winshun::Json(winshun::Json::Object {
            {"kind", "open"},
            {"path", text},
        }));

    winshun::Items items;
    items.push_back(std::move(item));

    const std::string usage = "disk C:\\";
    winshun::Item help("disk.help", usage,
                       tr(zh, "示例 · 把 C:\\ 换成任何文件夹或盘符",
                          "example · replace C:\\ with any folder or drive"));
    help.invalid()
        .withIcon("color:#185FA5")
        .withScore(90)
        .withCopy(usage)
        .withAutocomplete(usage)
        .withPreview(usage + "\n\n"
                     + tr(zh, "扫描只读取目录条目，不打开任何文件；junction 与符号链接只计数、不跟随。",
                          "Scanning reads directory entries only; junctions and symlinks are counted, "
                          "never followed."));
    items.push_back(std::move(help));
    return items;
}

struct DiskView : winshun::Plugin {
    std::string name() const override { return "ra9fael.diskview"; }

    // Runs after serve() has returned: close the window and join its thread so
    // the process exits with no UI thread left behind.
    ~DiskView() override { diskview::closeViewerAndWait(1500); }

    winshun::Items query(const winshun::Query& q) override
    {
        const bool zh = chinese();
        const std::string text = winshun::trim(q.text);
        if (text == "help" || text == "?" || text == "帮助")
            return emptyItems(zh, {});
        if (text.empty())
            return emptyItems(zh, diskview::loadViewConfig(dataDir()).lastPath);
        return pathItems(text, zh);
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        if (a.action != "open" && a.action != "default")
            return {winshun::effect::keep()};
        const winshun::Json data = a.item["data"];
        const std::string path = data["path"].isString() && !data["path"].toString().empty()
                                     ? data["path"].toString()
                                     : winshun::trim(a.text);
        const bool zh = chinese();

        diskview::ViewerSpec spec;
        spec.initialPath = winshun::wide(path);
        spec.zh = zh;
        spec.theme = theme();
        spec.dataDir = dataDir();
        spec.defaultSunburst = defaultSunburst(settings());
        // The host's look (nativeLook and the rest): the window paints itself
        // with it, so it looks like Win顺. An older host sends none.
        spec.appearance = appearance();

        std::string error;
        if (!diskview::openViewerWindow(spec, error)) {
            winshun::debug("ra9fael.diskview: viewer window failed: " + error);
            return {winshun::effect::notify(tr(zh, "无法打开磁盘透视窗口：", "Could not open the disk view window: ")
                                            + error),
                    winshun::effect::hide()};
        }
        winshun::debug("ra9fael.diskview: viewer window opened");
        return {winshun::effect::notify(tr(zh, "磁盘透视已打开（Esc 关闭）", "Disk view opened (Esc closes)")),
                winshun::effect::hide()};
    }

    void settingsChanged(const winshun::Json& s) override
    {
        // The base class already replaced settings(); nothing cached here.
        winshun::debug("ra9fael.diskview: settingsChanged");
        (void)s;
    }

    void appearanceChanged() override
    {
        // The host changed its look (theme or style): the open window takes
        // the new colours.
        winshun::debug("ra9fael.diskview: appearanceChanged");
        diskview::updateViewerAppearance(appearance());
    }

    void cancel(long long queryId) override
    {
        // Queries build two rows and touch no file system; there is nothing to
        // abort. The window's scan has its own cancel button.
        winshun::debug("ra9fael.diskview: cancel " + std::to_string(queryId));
    }
};

} // namespace

int main()
{
    return DiskView().serve();
}
