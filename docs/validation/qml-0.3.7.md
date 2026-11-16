# 0.3.7 控件风格 / 独立列菜单 / Dock bar / 结构条 / 不透明 / 合并栏

本轮只做本地构建、验证、打包；未安装升级、未发布 GitHub Release，未同步插件库。

## 八项修复与验证

1. **控件风格对齐 WinShun**：新建 `ThemedButton.qml`（control/controlBorder/controlHover/pressed token + controlRadius + Phosphor 图标），扫描/暂停/继续/取消/扫描问题/扫描设置按钮全部换用；`ThemedComboBox` 背景改 control/controlBorder；`palette.base`/`palette.button` 改 `theme.control`。Mica 下按钮与输入控件保持不透明，见 `default.png`（工具栏带图标）。
2. **列显示按列表独立**：每表右侧固定 26 px rail（`window.railWidth`），顶部 list-numbers 按钮开各自的列菜单（objectName `columnsButton:tree|file|legend`）；菜单无自动关闭（删除 Timer/HoverHandler）；name/size 锁定不进菜单；`columnWidths` JSON 扩展 `hidden:{tree,file,legend}`，旧配置把遗留 `scanSettings.hiddenColumns` 迁移为 file 面板隐藏列。菜单条目顺序正确（勾选项→分隔线→列宽复位），见 `columns-menu2.png` 与放大图 `menu2-zoom.png`。列布局预算改为可用宽度减 rail。
3. **Dock 删除冗余 bar**：`PaneHeader` 新增 `chrome` 属性（Dock 下 implicitHeight 0、visible false），tree/legend/chart/file 均按 `chartLayout !== "dock"` 绑定；dock 模式标签栏下方无恢复/最大化 bar，见 `dock-tabs.png`。
4. **图表结构条中性色**：`tree.h` `Tile` 新增 `bool expanded`（子级成功布局时置位），`ChartItem.cpp` pass1 中 expanded 文件夹磁贴填 `chromeFill()` 而非分支色。扩展名高亮模式下结构条不再读作文件块，见 `extension-highlight.png` 与放大图 `strip-zoom.png`。
5. **放大层与悬停卡片不透明**：`ThemeBridge` 新增 `solidSurface` token（= `over(surface, menuBackground)`，Mica 下恒不透明）；fullscreenLayer 背景与 hoverCard 改用它。表格组最大化后整层不透明，见 `maximized.png`。
6. **扫描按钮 glyph**：扫描→arrow-clockwise、暂停/继续→pause/play、取消→x、扫描问题→warning、扫描设置→gear（PhosphorAliases.h 既有映射），见 `default.png`。
7. **文件视图合并 bar**：`filePaneHeader` 改为自建 Rectangle（标题仅非 dock 显示 + 筛选 TextField `objectName "filterField"` + 当前/整个扫描 + 含子目录 + 子串/模糊），删除二级工具栏与筛选栏右侧放大缩小按钮；`window.filterField` alias 链保持有效；`fileToolbarFits()` 已删除。见 `file-filter.png`。
8. **网格线淡化**：`gridColor()` alpha 0.5 → 0.3，见各表截图。

## 通过

- 构建：`cmake --build build-plugin --config Release --target diskview-qml diskview-qml-plugin diskview-qml-app` 通过（C4305/C4456 为既有警告）。
- fixture 自检 `--self-test --fixture-only`：treemap 模式 `columns=72->96 neighbor=72->48 autofit=57 total=869/869,legend-name-grip PASS`；sunburst（`--view-mode 1`）`chart=double,center … PASS`。
- CTest：`diskview_logic`、`diskview_dock`、`diskview_chart_geometry`、`diskview_list_model`、`diskview_scan_settings` 通过；`diskview_protocol` 为既有失败（"timed out waiting for plugin state"，与本轮无关）。
- 视觉截图（1280×840，内置 fixture，`QT_QPA_PLATFORM=windows`，`WS_VISUAL_STATE` 驱动）：见本目录 `default.png`、`columns-menu2.png`、`menu2-zoom.png`、`dock-tabs.png`、`maximized.png`、`file-filter.png`、`extension-highlight.png`、`strip-zoom.png`。新增 `maximized` 视觉状态（tables 组最大化）用于第 5 项验证，已加入 QmlHost 白名单。
- 打包：`tools/package-qml.py --out dist` → `dist/ra9fael.diskview.qml-0.3.7.wsplugin`（8 文件，3006 KB），SHA256 `8883B955…E577`。

## 未测与限制

- 沙箱限制：插件进程无法写 `%TEMP%`（仅 opencode 批准目录可写，但应用写入报 "Unknown error"），截图改存仓库 `build-plugin/shots/` 后拷入本目录。
- 未在本机 WinShun 宿主重新安装 0.3.7 插件 DLL（沿用本轮不发布约定）；本机仍安装 0.3.6（SHA256 `7F3254B3…CA55`）。真实升级链路需发布时用 `tools/verify-installed-upgrade.ps1` 覆盖。
- Mica 下 solidSurface 的实际观感仍需真实 Win11 d3d11 宿主复核（截图为 windows 平台软件路径）。
- WinShun 宿主有 3 个进程在运行，未重启、未触碰。
