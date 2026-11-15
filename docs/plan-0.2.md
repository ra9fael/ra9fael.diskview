# 0.2 改进方案（8 项）

状态：方案，待确认后实施。顺序按“体感收益 / 风险”排。

---

## 1. 可视化填满背景，缩放只放大细节

### 现状
- `ChartItem::setZoom` 允许 `0.5–4.0`，所以整体可以被缩小。
- QML 里 `ChartItem` 有 `anchors.margins: 8`，`rebuildGeometry`/`paint` 内又有
  `inset=8`，四周共 16 px 空白。
- 缩放围绕控件中心，不跟指针。

### 方案
1. 缩放范围改为 **1.0–12.0**，初始 1.0 即铺满；不再允许整体变小。
2. 去掉 QML 的 8 px 边距；内部 inset 降到 0（树图）或 2 px（圆盘描边留白）。
3. 滚轮缩放以指针为锚点：
   `pan' = pointer - (pointer - pan) * (newZoom/oldZoom)`，再夹紧平移范围。
4. 平移夹紧：内容边缘最多到控件边缘，避免把图拖丢。
5. 圆盘仍按短边取半径（保持正圆），但用满短边；宽屏多出的空间用于悬停卡片/图例，
   不拉伸成椭圆。
6. `resetView()` 继续是“回 1:1 并居中”，图标不变。

### 验收
- 打开即四边贴满（除 1–2 px 描边）。
- 滚轮只放大；放大后指针下的区块保持在指针下。
- 100/125/150/175% 缩放下无模糊、无溢出。

### 涉及
`ChartItem.cpp`（setZoom/rebuildGeometry/paint/wheelEvent/hitTest）、`Main.qml`（ChartItem 边距）。

---

## 2. 可视化配色

### 现状
16 色哈希调色盘，色相和明度不统一，相邻大块容易撞色；文件夹固定灰。

### 方案：分类色板 + 感知均匀
1. 采用 **Okabe–Ito（色盲友好）为骨架**，再补一组同明度/同饱和的扩展色：
   - 主色：`#0072B2 #E69F00 #009E73 #CC79A7 #56B4E9 #D55E00 #F0E442 #4D4D4D`
   - 扩展：`#8C6D31 #5F4690 #1C7C54 #B03A5B #2E86AB #C58B00 #6B8E23 #A0522D`
   - 统一降到 S≈0.55、L≈0.55 的区间，避免荧光色。
2. 按扩展名哈希稳定映射（同一扩展名跨扫描颜色不变），但**按大小排序后的前 N 个
   扩展名**用调色盘顺序分配，让最大块彼此色相拉开；哈希只用于其余。
3. 文件夹保持中性灰 `#8B93A1`（深色 `#4A5568`），与文件彩色区分。
4. 渐变改为 HSL 明度 ±6%，不要在 RGB 上 `lighter/darker`（会偏色）。
5. “其他/无扩展名”用低饱和蓝灰，不与主色竞争。
6. 选中/悬停仍用系统 accent，不参与分类色。

### 验收
- 同一屏最大 8 个块颜色两两可辨；灰度打印仍能靠明度区分。
- 图例与方块/圆盘颜色完全一致。

### 涉及
`ExtensionColors.h`、`ChartItem::fillFor/fillBrush`。

---

## 3. 双击进入子目录 1 秒内卡顿

### 已定位/已修
- 文件面板排序每次比较构造两个字符串（已改为数值比较）。
- 图表 Top-K 用 `min_element` 扫 256 个（已改堆）。
- 模型导航整表 reset（已改为“行没变就不 reset”）。

### 剩余怀疑
- `FileListModel` 递归列出新目录**整个子树**的文件并排序：大目录 10 万文件约
  100 ms，百万级约 1 s。
- QML ListView 在 `beginResetModel` 后重建可见委托。

### 方案
1. **先量化**：加 `DISKVIEW_TRACE_TIME` 计时（模型/导出/文件面板/QML 帧），
   在真实 C: 上抓一次双击的分解耗时；不再盲改。
2. 文件面板改 **后台构建**：
   - 工作线程按当前根/过滤条件收集并排序；
   - 用世代号丢弃过期结果；
   - 构建期间面板顶部显示“正在列出…”，列表可继续交互；
   - 避免在大目录上阻塞 GUI。
3. 大目录默认 **只列当前目录的直接文件**，新增“包含子目录”开关（默认关）；
   需要整棵子树时显式打开，并在状态栏显示文件数。
4. 可选：结果超过 20 万条时只保留“最大的 20 万”，并提示“按大小截断”，避免排序
   与内存双重压力。
5. 图表导出保持扁平/堆实现，必要时把嵌套 `QVariantMap` 换成扁平数组（较大改动，
   先看第 1 步数据再决定）。

### 验收
- 双击进入 10 万文件目录 < 100 ms 出可见行；百万级 < 300 ms 且不冻结。
- 构建中滚动、切换选择不卡。

### 涉及
`FileListModel.*`、`ScanSettings`（子目录开关）、`Main.qml`（忙碌提示）。

---

## 4. 列表列宽可调

### 现状
只有树表的“% 父级”列有一个拖动手柄；文件表用固定宽度；扩展名表固定。

### 方案
1. 新增 `ColumnWidths`（C++ 或 QML QtObject）：
   - 三套独立：`tree`、`files`、`legend`；
   - 每列有 min/max/默认值；持久化到 `ScanSettings` 的 `columnWidths`。
2. 每个表头两列之间放 4 px 拖动带（`SplitHCursor`），拖动改宽度；
   双击拖动带恢复默认宽度。
3. 表头与行绑定同一宽度来源；窄面板仍按阈值自动隐藏次要列（现有逻辑保留）。
4. 名称列最小 80 px，其余列最小 40 px；总宽不足时只压缩名称列。
5. 文件面板与树面板完全解耦（现有两套对象保留，补持久化）。

### 验收
- 拖动任一列宽，表头与所有行同步；重启后恢复。
- 三套宽度互不影响。

### 涉及
`Main.qml`（表头/行绑定、拖动手柄）、`ScanSettings.*`。

---

## 5. 主题背景与 WinShun 一致（照 tio 的 appearance 契约）

### 关键发现（看 ra9fael.tio 的实现）
宿主**已经**在 `initialize` / `appearance` 里下发完整的 `nativeLook`，不是等
上游：

- `winshun::Plugin::appearance()` 返回整个 params；
- 字段：`uiStyle`（fluent/brand/tactile/liquid）、`uiSize`
  （compact/normal/roomy，文档示例也有 standard）、`corners`、`transparency`、
  `nativeLook.style/dark/rounded`；
- 颜色（`#AARRGGBB`）：`background` `layer` `page` `windowEdge` `divider`
  `text` `subtext` `faint` `hover` `pressed` `tagFill` `control`
  `controlHover` `controlBorder` `inputFocused` `menuBackground` `menuHover`
  `menuEdge` `accent` `accentFill` `accentLine` `accentText` `textSelection`
  `caret` `danger` `dangerFill`；
- 圆角：`cardRadius` `controlRadius` `largeControlRadius` `smallControlRadius`
  `menuRadius` `menuItemRadius` `menuPadding`。
- 透明效果下 `layer`/`hover` 可能是半透明，要先叠到 `background` 上。

tio 的做法（`qmlapp/ThemeBridge.*`、`qmlapp/qml/DesignTokens.qml`）：
1. `initialized()` 和 `appearanceChanged()` 里 `core().setAppearance(appearance())`；
2. `Look` 结构保存所有字段，缺失的用一套 fluent 近似值兜底；
3. `ThemeBridge` 把 `background/layer/page/divider/...` 映射成窗口/卡片/边框/
   文本等 QML 属性，半透明色用 `over(color, base)` 合成；
4. 圆角在“直角”设置下返回 0；
5. QML 用 `DesignTokens` 单例读取，一处 `changed()` 全窗口重绘。

### 我们的落地方案（不再等上游）
1. `ScanSettings` 之外新增 `AppearanceTokens`（或直接扩展 `ThemeBridge`）：
   - 用现有 `winshun::Plugin::appearance()` 解析，不引入新权限；
   - 保留 `appearance` 当前值，窗口重开仍能立即用上（tio 就是这么做的）。
2. `plugin_main.cpp`：恢复 `appearanceChanged()`（之前为了“设置同步”被我们关掉，
   但外观 token 不受该限制），调用 `QmlHost::setAppearance(json)`。
3. `ThemeBridge::apply()` 改为优先用 `nativeLook`：
   - `window = over(background, page)`、`surface=over(layer, window)`、
     `border=over(divider, surface)`、`hoverRow=over(hover,surface)`、
     `selection=over(tagFill,surface)`、`accent/accentFill/accentText`、
     `error=danger`；
   - `radius`/`controlHeight`/`fontSize` 由 `corners`、`uiSize` 决定；
   - 缺失字段回退现有浅/深表。
4. 深色标题栏：`DWMWA_USE_IMMERSIVE_DARK_MODE`（Windows 10 20H1+）。
5. 不碰宿主设置同步（那是另一件事，仍按你的要求关闭）。

### 验收
- 四种 style × 深/浅：窗口背景、卡片、边框、文字、选中色与 WinShun 一致；
- 切换“圆角/直角”“界面大小”后窗口跟着变；
- 老宿主没有 `nativeLook` 时回退到现有配色，不崩。

### 涉及
`ThemeBridge.*`、`plugin_main.cpp`、`QmlHost.*`、`ScanTree` 无关。

---

## 6. ComboBox 图标太大且丑（照 tio 的 TokenComboBox）

tio 的 `qmlapp/qml/TokenComboBox.qml` 已经解决了同一问题，直接采用其结构：

- `background`: `controlRadius` 圆角，`control`/`inputFocused` 底色，
  `controlBorder`/`accent` 1 px 边框；
- `contentItem`: 文本左 10、右预留指示器，`fontCaption`，省略；
- `indicator`: 10×6 的线条箭头，用 `muted` 色、1.5 px 描边（不是大三角）；
- 弹出：`card` 底、`border` 边框、`controlRadius` 圆角、4 px 内边距、
  每项 `smallRadius` 悬停 `hover`、最高 320 px、带滚动条。

我们落地时把 `muted`/`card`/`hover` 换成第 5 项的 token；若想保留 PH 图标，
把 `indicator` 换成 `Glyph caret-down` 12 px，二者视觉一致，优先用 PH 以统一。

把所有 `ComboBox`（布局、搜索范围、匹配方式、层级数）替换成该组件。

### 验收
- 任何缩放下箭头 12 px、不糊；与工具栏其他 PH 图标同风格。

### 涉及
新增 `ThemedComboBox.qml`、`Main.qml`、`CMakeLists.txt`。

---

## 7. 全屏按钮独立成栏，所有面板易于 Dock

### 现状
“表格全屏/图表全屏”在图表工具栏里；Dock 只能拖标题，入口分散。

### 方案
1. **工作区工具栏**（单独一行，位于搜索栏下）：
   - 左：布局菜单、恢复默认布局；
   - 中：四个面板开关（树/文件/扩展名/可视化），点击显示/隐藏；
   - 右：每个可见面板一个“全屏”按钮 + “全部还原”。
2. **每个面板标题栏**（Dock 中始终存在）：
   - 拖动手柄、图标、标题；
   - 右侧按钮：全屏、菜单（停靠到左/右/上/下、合并为 Tab、浮动）；
   - 双击标题全屏/还原。
3. Dock 保持现有自由拆分/Tab/比例持久化；补齐“浮动面板”在窗口内的自由定位。
4. 全屏仍走独立 `z:10000` 浮层（已验证不被遮挡），Esc 还原。
5. 移除图表工具栏里的“表格/图表全屏”按钮，避免两套入口。

### 验收
- 从任一标题栏可完成拆分/Tab/全屏；工具栏开关与布局状态一致。
- 四种布局预设与自由 Dock 互切不丢面板。

### 涉及
`Main.qml`（工具栏/标题栏）、`DockWorkspace.qml`、`DockState.*`。

---

## 8. 地址栏调用 WinShun 模糊搜索

### 宿主能力
SDK 提供 `Plugin::tool("search_files", args)`，受 `winshun:search` 权限控制，
可与 WinShun 的 MCP 工具同名同参（`search_files`、`search_content`、
`recent_files`、`list_folder`、`get_file_info`、…）。

限制：`tool()` 只能在 `serve()` 的协议线程调用，QML 在 GUI 线程。

### 方案 A（推荐先做，零协议改动）
用 `ws` CLI（已有 `processes` 权限）：
- 地址栏输入停顿 250 ms 后调用
  `ws find "<text>" --json -n 12 --folders`（或 `--files` 一并）；
- 解析 JSON 路径，弹出候选列表（图标区分文件/目录、显示父目录）；
- 选中目录 → 填入并扫描；选中文件 → 填入其父目录；
- 支持拼音/中文（`ws` 自带）。
- 250 ms 防抖 + 结果世代号丢弃过期；命令失败时静默回退为普通路径输入。

### 方案 B（上游/后续）
在 `serve()` 线程包一层请求队列，GUI 通过信号排队，协议线程执行 `tool()` 并回传；
优点是不起进程、速度快。需要新增 `winshun:search` 权限（用户需再次确认），
并处理线程归属限制。

### UI
- 地址栏变成“可输入 + 下拉建议”；
- 键盘 ↑/↓ 选择，Enter 应用，Esc 关闭；
- 建议项右侧显示“目录/文件”和大小；
- 无结果时显示“未找到，按 Enter 扫描当前路径”。

### 验收
- 输入“下载”“bg”等，200–500 ms 内出 WinShun 索引结果；
- 选中后开始扫描正确路径；
- 无 `ws`/索引未就绪时不影响手动输入扫描。

### 涉及
`QmlHost`（运行命令/解析 JSON）、`Main.qml`（建议弹层）、`plugin.json`（若走方案 B）。

---

## 实施顺序与预估

| 顺序 | 项 | 风险 | 预估 |
|---|---|---|---|
| 1 | 图表铺满 + 缩放锚点 | 低 | 0.5 天 |
| 2 | ComboBox + 列宽可调 | 低 | 1 天 |
| 3 | 配色 | 低 | 0.5 天 |
| 4 | 工作区工具栏 + 面板标题栏 | 中 | 1.5 天 |
| 5 | 主题背景（系统调色板） | 中 | 1 天 |
| 6 | 文件面板后台构建 + 直接文件开关 | 中高 | 2 天 |
| 7 | 地址栏 WinShun 搜索（方案 A） | 中 | 1 天 |
| 8 | 上游 token / 方案 B | 依赖上游 | 待定 |

每项完成后跑：模型/扫描/Dock 检查、100–175% 截图，并单独提交。

## 需要你确认
1. 文件面板默认是否改为“只列当前目录的直接文件”（提供“包含子目录”开关）？
2. 地址栏搜索先用 `ws` CLI（方案 A），还是等协议桥（方案 B）？
3. ~~主题等上游~~ 已确认宿主现在就下发 `nativeLook`（tio 在用），无需等上游；
   直接照 tio 接入。
