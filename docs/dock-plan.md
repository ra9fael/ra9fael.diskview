# Disk View QML 下一轮改进计划

状态：执行中。每个阶段完成并验证后提交一次（Angular 格式），重要节点打 tag。

## 范围调整（2026-11-15，用户确认）

- 不替换、不退出、不重启用户当前 WinShun，也不为验证升级本机宿主。
- 宿主设置与四风格主题同步等待上游合并，本轮明确不做；窗口使用默认独立主题和本地扫描设置。插件 `settingsChanged`、`appearanceChanged` 暂不覆盖窗口。
- 继续插件本身的相关验证、说明更新和 0.1.4 本地打包。不把宿主同步或管理员宿主实测写成已经完成。

## 执行记录（2026-11-17）

- 0.3.11：树与可视化双向选中同步，树高亮与延后定位；真实确认按钮异步回收在 C: 部署运行通过，回收站原路径及内容匹配。D: 开发目录运行限制见 `validation/qml-0.3.11.md`。
- 0.3.10：工具栏使用「预设布局」菜单，支持列表在上、下、左、右四种 Dock 预设；手动调整显示「自定义」并保存。分隔线可见，拖动、合并和最大化继续可用。
- 文件工具栏统一高度、预留列菜单空间，下拉面板保证文字宽度；树与文件表共用百分比单元格。删除弹窗采用内容布局，永久删除使用确认/取消，操作结果显示在弹窗和状态栏。
- 图表颜色绑定窗口主题 token，支持实时切换；列宽自检逐个激活树、文件、扩展名面板，检查拖动和自动适配，输出明确 PASS/FAIL。
- 删除路径转换为 Windows 原生分隔符。D: 测试文件永久删除通过；回收操作仍被未携带回收标志的 Shell 回调中止，尚未完成端到端修复。验证记录见 `validation/qml-0.3.10.md`。

### 0.3.9

- 默认布局改为上下结构：上方一个 Dock 标签组收纳树视图、文件视图、扩展名视图，下方整宽显示可视化视图；`DockState::defaultLayout()` 与 `diskview-dock-tests` 同步更新。
- 旧四种预设（tablesTop/Bottom/Left/Right）退役：`ScanSettings::chartLayout` 迁移为 `dock`，读取旧设置文件时自动落到 Dock 默认；工具栏保留"布局 自由 Dock"按钮（点击恢复默认 Dock）。
- `DockWorkspace` 重构为"槽位容器 + 面板锚定"：每个 tab 组生成 body 容器，激活面板 `anchors.fill` 进容器，替代逐面板手写 x/y/width/height（对 ColumnLayout 面板不可靠）。
- 修复 `Metrics` 单例未注册问题：`set_source_files_properties(QT_QML_SINGLETON_TYPE)` 必须在 `qt_add_qml_module` 之前调用，否则 qmldir 缺 `singleton` 标记，`metrics.*` 全部为 undefined（标签条、行高、状态栏一起塌陷）。
- 自检适配：列手柄与列菜单检查在目标面板非激活标签时按"未显示"处理（`legend-not-shown`）；`--maximize` 接受 tree/files/legend/chart/tables。
- 全套自检 PASS（拖拽合并、拖出、分割线、四面板最大化/还原、列宽、列菜单、持久化）；`diskview-dock-tests`、`diskview-scan-settings` 通过。

## 执行记录（2026-11-14）

- 自由 Dock 已接入第五种布局模式，保留原四种预设；四个现有面板只移动父项，不复制扫描数据、不重建图表。
- `DockState` 校验四面板唯一性，支持上下左右拆分、中央合并 Tab、Tab 激活、比例调整、无效布局拒绝；布局以 `dockLayout` JSON 对象保存。
- `DockWorkspace` 使用 PH 标题图标、拖拽罗盘和落点预览。标题双击或按钮全屏，Esc 还原；工具栏可恢复默认 Dock。
- 新增 `diskview-dock-tests`，布局连续移动、唯一性、JSON 往返、比例与矩形边界测试通过。
- `qmlapp/tests/check-dock.ps1` 通过方块/圆盘 × 100/125/150/175% 缩放及重启验证；通过窗口发送真实鼠标事件，覆盖 Tab 合并、拖出、分割线调整。
- 仍未完成：四种宿主风格的真实 token 桥接与深浅色矩阵（目前协议仅提供 light/dark，不能把假定的 style 后缀当作已支持）；管理员宿主实际快扫；最终版本打包、安装与版本标签。
- 当前 Dock 支持窗口内停靠和 Tab 拖出重新停靠，不提供独立桌面浮窗。

## 背景

0.1.3 已交付：模型扁平数组 + CSR、图表下钻不动树、四向布局、标题条、渐变、主题跟随、图例联动、`elevated` 权限声明。用户在使用中提出 8 个问题，本计划逐条落地。

## 问题与方案

### 问题 1：扫描应该直接用 WinShun 的管理员插件进程

**现状**：`plugin.json` 已声明 `elevated`，但 `ScanService.cpp` 仍然在扫描时自己 `ShellExecuteEx(..., "runas")` 弹 UAC 启动 helper。

**WinShun 机制**：`elevated` 的语义是"WinShun 自己以管理员身份运行时，插件跟随 elevated runner 以管理员运行"（`src/plugin/Elevation.h`）。它不会让普通权限的 WinShun 自动提权，插件也不应该自己申请 UAC。

**方案**：
- 删除 `ScanService` 里的 `runas` helper 路径（`scan_helper.h` 的 `ShellExecuteExW runas` 启动）。
- 进程启动时检测自身令牌：已是管理员 → MFT 快扫直接进行，不弹 UAC、不启 helper。
- 不是管理员 → 直接用普通遍历，状态栏与"扫描问题"里说明"当前 WinShun 未以管理员身份运行，快扫不可用"。
- 扫描设置里展示管理员状态一行。
- helper 的命名管道服务端代码保留（本进程已是管理员时可直接在进程内读 MFT，不再跨进程）。

### 问题 2：树视图、文件视图与扩展名列表应绑定同一份数据状态

**现状**：树/文件用 `diskModel.viewMode` 切换且占同一个面板，扩展名列表独立；联动只有图例高亮一条。

**方案**：`DiskViewModel` 增加统一入口，四个面板全部只读它：
- `selectNode(id, source)`：点树/点列表/点图 → 更新 `selectedId`、联动图表高亮、扩展名行高亮。
- `selectExtension(ext)`：点扩展名 → 列表按扩展名过滤、图表其余块去饱和。
- `enterNode(id, source)`：图表双击或树导航 → 树展开到该目录、图表进入、列表切换、扩展名重算；树大纲根不动。
- `resetChart()`：图表回到当前树根并复位缩放平移。
- `viewMode` 拆开：树视图与文件视图成为两个独立面板（Dock 后各自可见），不再是二选一的 tab。

### 问题 3：实现 QML Dock 模式

**现状**：四向 `SplitView` 预设 + "表格/图表全屏"两个布尔，面板不能自由组合。

**方案**（新建 `qmlapp/qml/dock/`，纯 QML + 一个 `DockState` C++ 小类存 JSON）：
- 四个 dockable：`ChartDock`（方块/圆盘）、`TreeDock`、`FileListDock`、`ExtensionDock`。
- 布局树：`DockSplit`（orientation + ratio + first/second）、`DockTabs`（activeIndex + panes）、叶子 `DockPane`。
- 拖拽：标题栏 `MouseDragHandler`，目标上显示方位罗盘（上/下/左/右/中心成 Tab），松手重构布局树。
- 分割线可拖动调比例；Tab 组可拖出。
- 每个面板独立"全屏"：浮层 `z: 10000`，Esc 或还原按钮回到原位（问题 8 的解法）。
- 布局保存到 `WS_DATA_DIR/diskview-qml.json` 的 `dockLayout` 键；"恢复默认布局"按钮。
- 默认布局（2026-11-17 更新，用户确认）：上方一个 Dock 标签组收纳树视图、文件视图、扩展名视图，下方整宽图表。

### 问题 4：方块/圆盘子元素默认描边

**现状**：只有选中/悬停画描边。

**方案**：`ChartItem::paint` 里每个 tile/扇区先画默认细描边（主题边框色，1px，半透明），选中改强调色 2px，悬停 1px 强调色。标题条边界用不同颜色避免混淆。

### 问题 5：界面图标用 Glyph

**现状**：文字按钮"方块/圆盘/表格全屏/图表全屏/还原/列"等。

**方案**：插件内建轻量 `Glyph`（Phosphor 字体随包分发，`PhosphorGlyph.h` 同源码复制）。映射：
- 树视图 `tree-structure`、文件列表 `rows`、扩展名 `tag`、方块 `squares-four`、圆盘 `chart-donut`
- 全屏 `arrows-out`、还原 `arrows-in`、复位 `arrow-counter-clockwise`、关闭 `x`
- 拖拽把手 `dots-six-vertical`、上 `arrow-up`、下 `arrow-down`
- 返回 `arrow-left`、前进 `arrow-right`、上级 `arrow-up-left`
- 所有按钮保留 ToolTip。

### 问题 6：界面风格跟随 WinShun 设置

**现状**：只有 `themeName` light/dark 二值，颜色散落 `window.shade(...)`。

**方案**：`QmlHost` 增加 `themeTokens` QVariantMap（style、dark、window、surface、surfaceAlt、text、textMuted、border、accent、accentHover、selection、radius、controlHeight、spacing、fontFamily、fontSize）。QML 里所有 `window.shade(...)` 换成 `Theme.get("xxx")` 读取。图表 `ChartItem::setTheme` 接受整个 tokens 而不是 "light"/"dark" 字符串。默认值兜底当前配色，宿主不发就用它。

### 问题 7：可视化复位按钮

**方案**：图表工具栏加"复位"（`arrow-counter-clockwise`）：
- `ChartItem::resetView()`：zoom=1、pan=(0,0)、清 hover/selection。
- 若图表已下钻，复位同时回到当前树根（`diskModel.resetChart()`）。

### 问题 8：图表全屏后被文件列表覆盖

**现状**：`maximizedPane` 只是 `visible = false` 另一个 SplitView，层级和焦点不保证。

**方案**：Dock 全屏走独立浮层 `fullscreenLayer`（`anchors.fill`、`z: 10000`），全屏面板移入浮层渲染，Dock 树保持原位引用，还原时放回去。Esc 快捷键。

## 执行顺序

### 阶段 1：权限 + 数据联动 + 描边 + 复位
1. ScanService 去掉 runas，改为进程内管理员检测。
2. DiskViewModel 增加 selectNode / selectExtension / enterNode / resetChart。
3. ChartItem 默认描边 + resetView。
4. 面板联动改造。
- 验证：单元测试 + offscreen 截图 + 自检。
- commit：`feat(qml): in-process elevated scan and unified selection`、`feat(qml): chart strokes and reset`、`refactor(qml): bind the four panes to one state`

### 阶段 2：主题 tokens + Glyph
1. QmlHost::themeTokens + QML Theme 单例。
2. ChartItem 吃完整 tokens。
3. Glyph 组件 + 图标替换。
- 验证：standalone 默认配色截图不回归；宿主模式不同 style/dark 截图。
- commit：`feat(qml): follow the host theme tokens`、`feat(qml): glyph icons`

### 阶段 3：Dock 工作区
1. DockPane / DockSplit / DockTabs 组件。
2. 拖拽 + 罗盘 + 布局树重构。
3. 全屏浮层（问题 8）。
4. dockLayout 持久化 + 恢复默认。
- 验证：offscreen 拖拽模拟、布局 JSON 往返、全屏/还原、分割线拖动。
- commit：`feat(qml): dock workspace`、`feat(qml): dock fullscreen layer and persistence`

### 阶段 4：验证 + 发布
- 0.1.4 打包安装（`ws plugin install`）。
- 升级权限确认提醒用户。
- tag `v0.1.4`。

## 验证清单

- [ ] 管理员 WinShun：插件以管理员运行，快扫无 UAC。
- [ ] 普通 WinShun：插件普通权限，扫描不弹 UAC，界面说明快扫不可用。
- [ ] 点树/列表/图/扩展名，四个面板同步。
- [ ] 四 dock 拆分、合并、Tab 化、拖出。
- [ ] 分割线调比例后重启保持。
- [ ] 全屏面板不被别的面板盖住，还原回原位。
- [ ] 复位按钮回根+回 1:1。
- [ ] 方块/圆盘默认描边清晰。
- [ ] 四种 WinShun 风格 × 深/浅色下窗口与图表颜色一致。
- [ ] 100/125/150/175% DPI 不破。
