# 0.3 方案（图标 / 复位 / 箭头 / 图表参考 / 全屏按钮）

参考（已用本机 7897 代理 clone 并阅读源码）：
- SquirrelDisk 2（Rust + egui）：
  - `src/ui/sunburst.rs`：稳定 key 的扇形动画；`MIN_PX=4` 弧长以下折叠为
    “smaller items”；`MAX_SEGMENTS=14000`；中心半径 = 20% 总半径；
    环数 3–9；布局只在树/视图/尺寸变化时重算。
  - `src/ui/theme.rs`：`segment_color(mid, depth, is_file)` 用**角度中点**在
    循环色轮上取色，再按深度调亮/降饱和；文件额外更亮、更淡；聚合色是背景
    压暗。
  - `src/ui/treemap.rs`：同一套角度色；`MIN_AREA=90 px²` 以下折叠；
    只有矩形达到 `NEST_W/NEST_H` 才嵌套；布局按签名缓存；矩形/颜色动画。
  - `src/scan/local.rs`：一组 worker 从共享 BFS 队列取目录；进度用原子量；
    硬链接按 (dev, inode) 且 nlink>1 去重；远程按 120 ms/4096 条批量刷新。
- MangoDisk（Rust + Vue + Tauri）：
  - `src/lib/utils/presentation/treemap-layout.ts`：Squarified Treemap；
    默认 `minimumVisibleShare = 0.0075`（视口已知时用 600 px²/面积），
    小于阈值的尾部合并为 remainder 块；`squarify` 以“行最差长宽比不再变好”
    为切行条件；remainder 的条目数可空，不冒充完整统计。
  - 深度 1–6（矩形）/2–6（旭日），每图分别记忆；返回/前进；分析会话缓存。

---

## 1. QML 版应用图标与 GDI 不一致

### 原因
- GDI 版 `diskview.exe` 编译了 `diskview.rc`，里面有
  `IDI_ICON1 ICON "app.ico"` 和 VERSIONINFO；所以资源管理器、任务栏、
  Alt-Tab 都显示同一个图标。
- QML 版只有 `QIcon(":/app.ico")` 设置窗口图标，**exe 本身没有图标资源**
  （`qt_add_executable` 没有 `.rc`），任务栏/Alt-Tab 用的是可执行文件图标，
  因此显示通用图标。
- 插件列表里的图标来自 `plugin.json` 的 `icon: app.ico`，和 exe 图标是两处。

### 方案
1. 新增 `qmlapp/diskview-qml.rc`：
   ```
   #include <winver.h>
   IDI_ICON1 ICON "../app.ico"
   VS_VERSION_INFO VERSIONINFO ... OriginalFilename "diskview-qml.exe"
   ```
   产品名、描述与 GDI 版一致，版本号取自 `plugin.json` 的版本。
2. 在 `diskview-qml`（独立程序）和 `diskview-qml-app`（插件 DLL）两个目标都
   加入该 rc（DLL 用同一图标，便于资源管理器显示）。
3. `main.cpp` 启动时 `QGuiApplication::setWindowIcon(QIcon(":/app.ico"))`，
   对话框也统一。
4. 验收：`Get-Item diskview-qml.exe | Select VersionInfo` 有版本；资源管理器
   与任务栏图标与 `diskview.exe` 逐像素一致；插件列表图标不变。

### 涉及
新增 `qmlapp/diskview-qml.rc`、`qmlapp/CMakeLists.txt`、`qmlapp/src/main.cpp`。

---

## 2. 复位后可视化没有占满

### 原因
- `Main.qml` 里 `ChartItem` 仍有 `anchors.margins: 8`，四周 8 px 永远留白。
- `resetView()` 先 `setZoom(1)` 再 `setPan(0)`；两个 setter 在值未变时提前
  返回，不会重算几何。

### 方案
1. 去掉 `ChartItem` 的 `anchors.margins: 8`，让它铺满所在容器。
2. `resetView()` 无条件调用一次 `rebuildGeometry()`，再 `update()`；保证即使
   zoom/pan 已是默认值也按当前尺寸重排。
3. 容器 `Rectangle` 的 `radius` 对绘图无影响（`clip: true` 仍圆角裁剪），
   保持 4 px 视觉边；若要求严格贴边，可把 radius 设 0。
4. 验收：打开、复位、切换根目录后，树图四边无白边；圆盘只留 2 px 描边。

### 涉及
`Main.qml`、`ChartItem::resetView`。

---

## 3. 文件列表左侧箭头展开后收回仍朝下

### 原因
- 箭头绑定 `ExpandedRole`（`rowItem.expanded`）。
- `toggleExpanded` 已改成 `beginInsertRows` / `beginRemoveRows` 的增量更新，
  但**没有为被切换的父行发 `dataChanged`**，模型只通知了插入/删除的行，
  父行的 `expanded` 角色没有被重新读取，于是箭头停在旧状态。

### 方案
1. 在 `toggleExpanded` 的展开/折叠两个分支末尾，对父行发
   `emit dataChanged(index(row), index(row), {ExpandedRole})`。
2. 折叠时若 `row` 找不到（防御），仍走整表 `applyRowChange`，保证一致。
3. 增加模型单测：展开后 `data(row, ExpandedRole)==true`，折叠后 `false`；
   再截图确认 ▾/▸。
4. 顺带检查：筛选模式仍走整表重建，不受影响。

### 涉及
`DiskViewModel::toggleExpanded`、`model-tests.cpp`。

---

## 4. 参考 SquirrelDisk / MangoDisk 的扫描与可视化

### 4.1 颜色：改成“角度色轮 + 深度明暗”（SquirrelDisk 的核心）
不再按扩展名哈希取色，而是像 SquirrelDisk 一样**让颜色跟着图上的位置走**：
- 维护一条循环色轮（8–12 个同明度色相，首尾相接）；
- 每个扇区/方块用它的**角度中点**或父块内相对位置在色轮上取色；
- 深度每加一层：明度 +3%、饱和 −2.5%；文件再 +4% 明度、饱和 ×0.85；
- 聚合块用背景压暗的中性色，不参与色轮；
- 文件夹/文件种类仍用明暗区分，不再靠“随机色”。

这样相邻大块色相自然过渡，整张图协调；扩展名颜色只保留在图例（可选开关）。

### 4.2 太阳图
1. 几何：中心孔 = 半径 20%，环宽均分；环数 3–9（默认 4）。
2. **按像素弧长聚合**：中环弧长 < 4 px 的尾部合并为“较小项目”扇区
   （SquirrelDisk 的 `MIN_PX=4`），点击该扇区只看被折叠的条目。
3. **稳定 key + 动画**：每个扇区用节点 id（或 group key）标识；视图变化时
   从旧角度/半径过渡到新值（250 ms），缩放时被点的扇区“展开”到中心。
4. **交互**：单击进入、中心孔返回上级；悬停与列表/图例互亮；
   选中描边用主题 accent。
5. 段数上限（如 14000），超出直接聚合，避免绘制卡顿。

### 4.3 矩形图
1. 保留现有 Squarified 布局，但**按像素面积聚合**：面积 < 90 px² 的尾部
   合并为“较小项目”块（SquirrelDisk 的 `MIN_AREA=90`；MangoDisk 用
   0.75% 或 600 px²/视口面积，二者取更合适者）。
2. 只有矩形达到最小宽高（如 36×24）才继续嵌套；否则只画到该层。
3. 块内信息分三级：空间足够 → 名称 + 大小 + 占比；中等 → 名称 + 大小；
   小 → 只名称或省略。
4. 颜色同 4.1（角度/父块位置取色，深度调亮），不用扩展名哈希。
5. 深度 1–6，矩形与圆盘**分别记忆**；切换图表不重扫。

### 4.4 扫描
1. 保留 MFT 快扫；普通遍历已是 worker 池，补：
   - **硬链接去重**：`(dev, inode)` 且 nlink>1 只计一次（SquirrelDisk 的做法）；
   - 进度按文件数/字节/记录推进，可取消（已有）。
2. **内存会话缓存**：最近访问目录的“直接子项 + 大小”缓存 80 条，
   返回/前进不重扫；不落盘。
3. **扫描中增量出图**仅对小/中扫描启用（例如 < 50 万节点，每 120 ms 批量
   推送一次，SquirrelDisk 的批量节奏）；C: 全盘仍完成后出图，避免内存峰值。
4. 图表布局按签名缓存（树版本、视图、深度、尺寸、主题），只在变化时重算
   （SquirrelDisk 的 `sig` 做法）。

### 4.5 验收
- 相邻大块色相连续、深度越深越亮；灰度打印靠明度可分。
- 太阳图：弧长 < 4 px 的尾部成“较小项目”，点击可查看；单击缩放/中心返回。
- 矩形图：面积 < 90 px² 的尾部成“较小项目”；块内文字按空间分级。
- 深度分别记忆；返回/前进瞬时；取消保留已扫部分。

### 涉及
`ChartItem`（角度/半径/颜色/聚合/动画）、`tree.h`（布局聚合）、
`DiskViewModel`（每图深度、目录缓存）、`ExtensionColors`/主题色。

---

## 5. 全屏后右上角悬浮“还原”按钮

### 原因
`Main.qml` 的 `fullscreenLayer` 里有一个 `ToolButton`（“还原 (Esc)”），
与新的工作区工具栏“全部还原”、Esc 重复。

### 方案
1. 删除该悬浮按钮。
2. 保留：Esc、工作区工具栏“全部还原”、面板标题栏全屏按钮。
3. 全屏层只保留背景与 `fullscreenSlot`；确保工具栏在全屏时仍可见
   （它在工作区之外，已满足）。
4. 验收：全屏后无右上角悬浮按钮；三种还原入口都可用；自检截图更新。

### 涉及
`Main.qml`（fullscreenLayer）、`check-dock.ps1` 截图断言。

---

## 实施顺序与风险

| 顺序 | 项 | 风险 | 说明 |
|---|---|---|---|
| 1 | 图标 rc | 低 | 只影响资源 |
| 2 | 去边距 + reset 重排 | 低 | 视觉回归看截图 |
| 3 | 箭头 dataChanged | 低 | 加单测 |
| 4 | 图表参考重构 | 中高 | 分两次：先颜色/聚合/深度，再动画/缓存 |
| 5 | 删悬浮按钮 | 低 | 截图更新 |

每项单独提交并跑：模型/扫描/Dock/自检、100–175% 截图。

## 需要确认
1. 颜色是否改为“角度色轮 + 深度明暗”（SquirrelDisk 的做法），
   扩展名颜色只留在图例/可选？
2. 圆盘是否改为“单击进入、中心返回”（现在是双击）？
3. 扫描中增量出图是否只对小于 50 万节点的扫描启用？
