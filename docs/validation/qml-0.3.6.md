# 0.3.6 下拉框 / 滚动条 / Dock 标题 / 列宽 / Mica

本轮只做本地构建、验证、打包与安装；未发布 GitHub Release，未同步插件库。

## 六项修复与验证

1. **下拉框文本截断**：`ThemedComboBox` 改为按最长选项自适应宽度，并把 Basic 样式自带的 `leftPadding/rightPadding` 清零（原来和 Label 自己的内边距叠加，把文字区挤没）。层级框显示完整「自动」、布局框显示完整「表格在上」。磁盘切换弹窗的 `popupContentWidth` 改由 delegate 按实际字体上报，完整显示「C: (fixture)    内置视觉验收数据」。
2. **滚动条自动隐藏**：`StyledScrollBar` 闲置淡出、悬停显现。所有表格与弹窗的滚动条统一换成该控件。
3. **盘切换过长**：切换磁盘仍用 ComboBox，不随鼠标移出关闭；popup 宽度自适应，不再写死 440。
4. **重复 tab**：dock 模式下 `PaneHeader.showTitle = false`，并移除 `DockWorkspace` 里重复的标题 `Label`；标签栏与面板标题二选一。
5. **树视图列宽遮挡**：默认列宽由合计 874 收紧到 676，1280 宽窗口首次打开名称列不再被压缩；拖动、相邻列吸收、总表宽固定和自动自适应的语义不变。
6. **宿主主题 / Mica**：主窗口在宿主 `transparency=on`、`renderer=d3d11`、d3d11 图形 API、Win11 22H2+ 且系统透明开启、非高对比度时启用 Mica（liquid 风格用亚克力），否则回退为不透明配色；d3d11 下保持 alpha swapchain，popup 小窗口不启用。

## 通过

- `cmake --build build-plugin --config Release --target diskview-qml diskview-qml-plugin diskview-qml-app`：通过（Qt 头文件 C4702 警告为既有）。
- fixture 自检 `--self-test --fixture-only`：`columns=72->96 neighbor=72->48 autofit=57 total=895/895,legend-name-grip PASS`。
- CTest：`diskview_logic`、`diskview_dock`、`diskview_chart_geometry`、`diskview_scan_settings`、`diskview_scan_behaviour`、`diskview_list_model` 通过；`diskview_protocol` 为既有失败（stash 掉本次 QML 改动后仍失败，测的是原生 `diskview` 目标）。
- 视觉截图（1280×840，内置 fixture，`QT_QPA_PLATFORM=windows`）：见同目录 `none.png`、`drives.png`、`dock-tabs.png`。层级/布局下拉框文字完整，dock 标签无重复标题，树列完整。
- 打包：`tools/package-qml.py` → `dist/ra9fael.diskview.qml-0.3.6.wsplugin`（8 文件，2962 KB）。
- 协议：`tools/verify/smoke-plugin-qml.py`（从解压包运行，`WS_APP_DIR` 指向本机 WinShun）query/open/scan/theme/shutdown 通过。
- 安装：`ws plugin install ...0.3.6.wsplugin` 升级成功，`ws plugin list` 显示 0.3.6；已安装 `DiskViewQmlApp.dll` 的 SHA256 与本次构建一致（`7F3254B3…CA55`）。升级前备份了 0.3.5 插件目录。

## 未测与限制

- Mica 实际观感无法在当前环境确认：截图使用软件/Windows 平台，不满足 d3d11 + 真机条件；代码已实现条件判断与不透明回退，需在真实 Win11 d3d11 宿主下复核。
- 未重启正在运行的 WinShun（有 3 个进程）；新 DLL 在宿主下次启动时加载。
- 未发布 Release、未同步插件库；安装仅覆盖本机已安装插件。
