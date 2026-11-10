# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [0.3.0] - 2026-11-09

### 变更
- 界面与 HTML 交互原型逐项对齐：字体改为像素 token（正文 13 / 强调 13 半粗 / 次要 12 / 图块 11），图表标题从 20px 降为两级；列表头合并为一行（模式切换与筛选框同排），行高 28px，列头三列独立并与数据对齐；图表内不再双重内缩，可用高度提升到约 81%（1184×761 实测 613px）。
- 缩放与平移不再占用按钮：滚轮、空格拖动、中键拖动和 `0` 键覆盖同样能力，图表内不再有缩放和平移按钮行，也不再占用图内标题区。
- 状态栏第一行显示选中项名称、完整路径、大小与占比、读取问题徽标，以及进入 / 定位 / 更多三个按钮；第二行显示快捷键提示和图表像素占比。瞬时消息改为图表区右下角的 toast，4 秒自动消失；扫描状态只在徽标一处表达，不再重复出现在标题栏与顶部整行。
- “全部上级”从独立按钮改为命令表条目（`Command::Ancestors`），可从右键菜单和 Ctrl+K 操作搜索调用。
- 窄窗口下图表工具条分两行：第一行父级按钮与当前目录，第二行视图切换；状态栏同样分两行堆叠。

### 新增
- 普通扫描改为有界目录工作池（`parallel_scan.h`）：固定数量的 worker 只产出局部结果，由单一协调线程持有并修改树，不存在多线程同时改写同一节点。自动策略从 SSD 4、机械盘 1、网络 2、未知设备 1 起步；扫描设置提供 1 / 2 / 4 / 8。并发更高不承诺更快。
- 扫描设置面板：扫描方式、并发上限、介质策略、预览发布间隔与下轮排除。这些设置现在写入 diskview.json，下次启动恢复；扫描失败或取消也不会丢失。
- 扫描中可暂停 / 继续；F5 打开扫描设置，而不是静默重扫。
- 父级导航三处入口：当前目录旁带标题的“↑ 返回”、列表固定首行“..”、圆盘中心。返回后会重新选中刚离开的目录并恢复焦点。
- 每目录视图记忆：重新进入某目录时恢复选中项、筛选、滚动位置和列表模式。
- 图表缩放（25% 步进至 400%）、四向有界平移、空格拖动与中键拖动、适合窗口复位；命中测试计入缩放和平移。
- 统一命令表（`viewer_commands.h`），右键菜单、操作搜索（Ctrl+K）与键盘共用同一套可用条件与执行入口。右键菜单按“文件操作 / 导航 / 扫描”分组；图表条目、列表行和状态栏共用同一菜单，支持右键与 Shift+F10。
- 文件操作补齐：移至回收站（IFileOperation）、复制 / 剪切文件（CF_HDROP + Preferred DropEffect，资源管理器粘贴生效，快捷键 Ctrl+Shift+C / X）、永久删除（不经过回收站，先阅读警告再输入条目名称确认，无法撤销，不设快捷键）。扫描结果不完整不再阻止删除；扫描进行中与系统受保护项仍拒绝。只有 shell 确认成功后才更新快照。新增 clipboard:write 权限，安装时需确认。
- Direct2D 抗锯齿圆弧几何（`viewer_drawing.h`），设备或工厂不可用时回退 GDI。
- 扫描问题、容量统计口径与阶段计时改为非模态面板，阅读时仍可导航。
- 局部预览中等待父级关联的 MFT 记录计入 `pendingRecords`，扫描完成后才归入 `orphanRecords`。被排除的子树计入 `excludedItems`，不再算作读取错误。

### 修复
- 筛选框的提示文字在扫描和交互期间持续闪烁：主窗口补上 WS_CLIPCHILDREN，提示文字改为与控件同趟绘制，移动子控件不再触发无谓重绘；聚焦时提示文字按系统习惯隐藏。
- 扫描设置每次重启都会丢失，因为 diskview.json 只保存了上次路径。
- 容量与覆盖范围对话框是模态的，会阻塞父级导航。

## [0.2.0] - 2026-11-09

- 修复可执行文件图标，补齐打包中的 ICO 和资源脚本。 为窗口显式设置大小图标，随 DPI 更新并释放图标句柄。 同时包含当前工作区的 0.2.0 扫描和界面改动；新增 processes 权限，升级需要确认。
- 本次仅生成本地标签与安装包。实际验证范围见 docs/icon-validation.md；未进行 GitHub 发布。更新后需重新启动插件进程。

### Added
- Optional read-only MFT scanning for whole fixed NTFS drives, using a
  short-lived elevated helper and a restricted local metadata pipe. The UI
  stays unelevated; declined consent or helper failure falls back to standard
  enumeration with a visible reason. Adds the `processes` permission.
- Top breadcrumbs, separate back/forward history and parent navigation,
  editable indexed paths with Ctrl+L, persistent single-click selection and
  double-click folder navigation.
- Delayed full-path hover overlays, matching status text, synchronized chart
  and list highlighting, selection details and a locate-parent action.
- One/two-level layouts, small-item aggregation, resizable panes, narrow-window
  chart/list tabs, filtering, sorting, pagination and up to 1000 largest files.
- Nonmodal scan progress, immutable partial previews and previous-result
  recovery; incomplete results, unknown sizes and failures stay explicit.
- Keyboard focus navigation, a standalone `--view [path]` preview mode,
  updated light/dark styling, executable icon and version resources.
- MFT/parser/transport and native interaction regressions; the distribution
  includes the vendored WinShun reader's MIT license and all required sources.
- Fixed-drive capacity and usage in query results and clickable welcome cards.
- A Folders tab with immediate subfolder sizes and shares, including empty
  folders; the largest-files ranking now follows the current folder.
- Clickable skipped-directory details in the status bar.
- Window-message regression tests, deterministic visual snapshots, and a
  real-process protocol smoke test for scan, reopen, theme updates and shutdown.

### Fixed
- Drive-root and extended-path normalization, relative paths and forward slashes.
- Navigation through nested tiles, root-level file rows, breadcrumbs and Up.
- Failed scans expose the error while retaining an explicitly identified
  previous result; cancellation preserves complete or labelled partial data.
- Sunburst rings stay inside the chart at small sizes and high DPI; status
  text no longer overlaps breadcrumbs or totals.
- The path box follows drive-card scans; F5 also works from the path box.
- Reopening clears previous errors and cancellation state; shutdown destroys
  pending scan messages using their correct types.


## [0.1.0] - 2026-11-08

### Added
- Initial release. `disk` opens the disk view window; `disk <path>` opens and
  scans that folder immediately. Scanning is always user-triggered and only
  happens in the window.
- Recursive treemap (squarified, default) and recursive sunburst, switchable
  in the window (Ctrl+V) and via the `defaultView` setting. Culled entries
  leave honest gaps; parent totals are never redistributed.
- Drill down by clicking tiles / slices, go up with Backspace or the Back
  button, jump via status-bar breadcrumbs.
- "Largest files" list (up to 64 rows) with click-to-folder navigation.
- Hover details: kind, size, file/folder counts, unreadable entries, and a
  cluster-aligned allocation estimate for files.
- Scanner: cancellation with previous-result retention, per-node unreadable
  accounting (up to 32 paths listed), reparse points counted but never
  followed, `\\?\` long-path and UNC support, logical vs cluster-rounded
  sizes.
- The window paints itself with the host's own colours (`appearance.nativeLook`),
  falling back to built-in light and dark palettes; it follows a theme or style
  change while it is open.
- The `defaultView` setting decides the startup view; the saved config keeps
  only the last scanned path (`diskview.json` in the plugin data directory).
- Per-monitor V2 DPI awareness.
