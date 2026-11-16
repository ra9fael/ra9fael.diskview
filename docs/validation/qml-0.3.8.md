# 0.3.8 Dock 全屏聚焦栏 / 列菜单 toggle / 弹窗按钮 / Dock 不透明 / 主题设置文字

本轮只做本地构建、验证、打包与手动安装；未发布 GitHub Release，未同步插件库，未跑升级链路。

## 五项修复与验证

1. **Dock 全屏找回还原入口**：`fullscreenLayer` 顶部新增聚焦栏（objectName `fullscreenBar`，高 `metrics.paneHeader`）：左侧 Phosphor 面板图标 + 面板名（`dockWorkspace.title/icon`），右侧 `ThemedButton`「还原」（`corners-in`，objectName `fullscreenRestoreButton`，`compactHeight` 新属性支持矮按钮，ToolTip「还原 (Esc)」），点击调用既有 `window.maximizePane(window.maximizedPane)`；`fullscreenSlot` 顶部按栏高让位。预设布局的面板标题栏本就有还原按钮，聚焦栏按 `chartLayout === "dock"` 显示。见 `dock-maximized.png` 与放大图 `dock-maximized-restore-zoom.png`、`dock-maximized-title-zoom.png`。
2. **列显示 rail 按钮 toggle**：新增 `window.toggleColumnsMenu(panel, button)`（已开同面板→关闭；否则切换面板并弹出），三个 rail 按钮统一改调它；`columnsMenu.closePolicy` 改 `Popup.CloseOnEscape`，新增窗口级 dismiss 层（`MouseArea`，菜单打开时可见，z 100000、在 popup 之下）——菜单打开时点击 rail 按钮落在其上并关闭（真 toggle），点击窗口其他位置关闭但不穿透触发下方控件，菜单条目触发后显式 `close()`。自检新增 `verifyColumnsMenuToggle()`（程序化 开→关→切面板→收起）与 C++ 真实鼠标路径（向窗口发送 press/release 于 `columnsButton:tree`：开→关），自检行出现 `columns-menu-toggle`。见 `columns-menu.png`。
3. **弹窗按钮不再越界**：四个弹窗（扫描设置、主题设置、扫描问题、删除确认）删除自定义 `footer`，按钮行移进内容 ColumnLayout 末尾并换用 `ThemedButton`；Qt Quick 的 footer 停靠在 popup 底边但不计入隐式高度，按钮会压出/盖住边框。见 `scan-settings-dialog.png`、`theme-dialog.png`、`delete-confirmation.png`。
4. **Dock 空隙不透底**：`DockWorkspace` 背景 `theme.window` → `theme.solidSurface`（Mica 下 `theme.window` 为全透明，空隙会透出底层窗口材质/像素）；`DockState::place()` 对 x/y/w/h 取整到整像素（缩放比例取整后各槽仍精确铺满工作区），面板与状态栏之间不再有分数像素空隙。全屏层背景本就是 solidSurface。Mica 真机观感待宿主复核（截图为软件路径）。
5. **主题设置带文字**：工具栏「主题设置」从纯图标 ToolButton 改为 `ThemedButton`（paint-brush 图标 + 「主题设置」文字，minimumWidth 88），与「扫描设置」一致。见 `default.png`。

## 通过

- 构建（`scripts/devshell.ps1` 进 MSVC 环境 + `cmake --build build-plugin --config Release --target diskview-qml diskview-qml-plugin diskview-qml-app`）通过；仅 Qt 头文件的既有 C4702 警告。
- fixture 自检 `--self-test --fixture-only`：`…columns=72->96 neighbor=72->48 autofit=57 total=869/869,legend-name-grip columns-menu-toggle PASS`；sunburst（`--view-mode 1`）`chart=double,center … columns-menu-toggle PASS`。
- CTest：`diskview_logic`、`diskview_dock`、`diskview_chart_geometry`、`diskview_list_model`、`diskview_scan_settings` 5/5 通过；`diskview_protocol` 为既有失败（与本轮无关）。
- 视觉截图（1280×840，内置 fixture，`QT_QPA_PLATFORM=windows`，`WS_DATA_DIR=%LOCALAPPDATA%\Temp\opencode\diskview-038-shots`）：`default`、`dock-maximized`（新状态）+ 两张放大图、`scan-settings-dialog`、`theme-dialog`（新状态）、`delete-confirmation`、`columns-menu`、`dock-tabs`、`maximized`。`QmlHost.cpp` 白名单新增 `dock-maximized`、`scan-settings-dialog`、`theme-dialog`。
- 打包：`tools/package-qml.py --out dist` → `dist/ra9fael.diskview.qml-0.3.8.wsplugin`（8 文件，3036 KB），SHA256 `639828d15eaf5ea601799945b9338d0212a8c505cdf28b16f769a4ba68ae030d` + `.sha256`。
- 本机安装 0.3.8：旧 0.3.7 八个文件改名备份到 `%TEMP%\opencode\diskview-0.3.7-backup`，从 wsplugin 解包到 `C:\Users\renfei\AppData\Local\WinShun\plugins\ra9fael.diskview.qml\`，逐文件 SHA256 与包内一致，plugin.json 显示 0.3.8；插件数据目录未动。

## 未测与限制

- Mica 下 solidSurface 工作区背景与聚焦栏的实际观感需真实 Win11 d3d11 宿主复核（截图为 windows 平台软件路径）。
- 列菜单打开时点击其他位置只关闭菜单、不穿透触发下方控件（与 Windows 原生菜单一致）；打开 B 表菜单需两次点击（第一次先关 A 表菜单）。若要"点击直接切到 B 表菜单"需再做穿透合成。
- 本机 0.3.6 备份目录（`diskview-0.3.6-backup`）因旧 `diskview-qml.exe` 进程（PID 42196，0.3.6 镜像）可能仍驻留而保留未删；确认 0.3.8 满意后连同 0.3.7 备份一起清理。
- 未发布 Release、未同步插件库；插件升级链路（`tools/verify-installed-upgrade.ps1`）留到真正发布时做。
- WinShun 宿主多进程运行中，未重启、未触碰。
