# 0.3.9 默认 Dock 布局：三视图同组在上，可视化视图在下

本轮只做本地构建、验证与本机插件安装；未发布 Release、未同步插件库。

## 变更

1. **默认布局改为上下 Dock**：`DockState::defaultLayout()` 由「左列树/文件/扩展名、右侧图表」改为一个纵向 split —— 上方一个 `tabs` 组收纳 `tree`、`files`、`legend`（默认激活 `tree`），下方整宽 `chart`，比例 0.34。四个面板仍可拖拽合并、拖出、调整分割线与独立最大化。
2. **旧预设退役**：`ScanSettings::chartLayout` 只保留 `dock`，读取旧版本的 `tablesTop/tablesBottom/tablesLeft/tablesRight` 时自动迁移到 Dock 默认；已存在的 `dockLayout` 继续沿用。工具栏布局控件收敛为一个「布局 自由 Dock」按钮（点击恢复默认 Dock），去掉无对应控件的下拉箭头与四种预设选项。
3. **`DockWorkspace` 改为槽位容器 + 锚定**：每个 tab 组生成 body 容器（`dock-body-<path>`），激活面板 `anchors.fill` 进该容器。此前逐面板写 `x/y/width/height`，对 `ColumnLayout`（可视化视图）与带 Layout 附加属性的面板不可靠，会出现面板尺寸为 0 而标签条正常的情况。
4. **工具栏分组与状态栏统计**：顶部工具栏按「扫描位置 ｜ 扫描动作 … ｜ 主题设置 ｜ 布局」加分隔线；「当前目录 · 总量 · 项数」从图表面包屑行移到状态栏，面包屑只表示浏览位置。
5. **修复 `Metrics` 单例未注册**（既有缺陷）：`set_source_files_properties(qml/Metrics.qml QT_QML_SINGLETON_TYPE TRUE)` 必须在 `qt_add_qml_module` 之前调用，否则生成的 `qmldir` 缺少 `singleton` 前缀，`Metrics` 被当作普通组件加载，`metrics.*` 全部为 `undefined`（标签条高度、表格行高、状态栏高度一起塌成 0）。已在 `qmlapp/CMakeLists.txt` 两个 QML 目标上调整顺序。

## 通过

- 构建：`cmake --build qmlapp/build --parallel 2` 通过；仅 Qt 头文件的既有 C4702 警告。
- fixture 自检 `--fixture-only --self-test`：`0 12 regions transform navigate return list=sort,filter,files,expand,history,view drag chart=double columns=legend-not-shown,legend-name-grip columns-menu-toggle PASS`。覆盖默认分组断言（三视图同组在上、图表在下）、拖拽合并/拖出、分割线、四面板逐个最大化与还原、列宽拖拽与自动适配、列菜单开/关/切换、布局持久化；`--maximize files` 截图确认列表面板全屏后仍有「还原」聚焦栏。
- CTest：`diskview_dock`、`diskview_chart_geometry`、`diskview_scan_settings`、`diskview_scan_behaviour`、`diskview_list_model` 5/5 通过。`diskview_logic`（原生查看器 treemap 断言）与 `diskview_protocol`（插件冒烟）在 `HEAD` 干净工作树同样失败，属既有问题，与本轮无关。
- 截图（1280×840，内置 fixture，`QT_QPA_PLATFORM=offscreen` + 软件渲染）：`default-dock.png`、`dock-list-maximized.png`。

## 未测与限制

- Mica 下的实际观感需真实 d3d11 宿主复核（截图为软件渲染路径）。
- 旧版预设布局不再提供；保存过 `tablesTop` 等值的用户会看到新的默认 Dock，其 `dockLayout` 不受影响。
- 本机安装：`tools/package-qml.py --out dist` 生成 `dist/ra9fael.diskview.qml-0.3.9.wsplugin`（8 文件，3044 KB，SHA256 `e9c23b9e32ca56d2457caaa4a5aac43810b484760bce76fa1bec853747625adf`），`ws plugin install` 覆盖 0.3.8，`ws plugin list` 显示 `0.3.9 / on / ready`。未发布 Release、未同步插件库。
