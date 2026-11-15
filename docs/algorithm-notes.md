# 算法借鉴笔记（SquirrelDisk 2 / MangoDisk）

两个上游仓库都已用 7897 代理 clone 并读过源码：

- `adileo/squirreldisk`（Rust + egui，**AGPL-3.0**）
  - `src/ui/treemap.rs`、`src/ui/sunburst.rs`、`src/ui/theme.rs`、`src/tree.rs`
  - `src/scan/local.rs`、`src/scan/platform.rs`
- `harry0703/MangoDisk`（Rust + Vue + Tauri，**GPL-3.0**）
  - `src/lib/utils/presentation/treemap-layout.ts`
  - `.../hierarchical-treemap-layout.ts`、`.../sunburst-layout.ts`
  - `src/pages/analysis/components/md-analysis-treemap.vue`

> 许可提醒：两边都是 GPL/AGPL。我们的插件是 MIT，**不能整段复制它们的代码**。
> 下面记录的是算法、常量与布局策略（思想与数学），实现要按这些描述**独立重写**；
> Squarified Treemap 本身是 Bruls/Huizing/van Wijk 的公开算法。

---

## 1. 数据模型

| | SquirrelDisk | MangoDisk | 我们 |
|---|---|---|---|
| 存储 | `Vec<Node>`，`first_child/next_sibling` | 分析结果 + 按目录的层级投影 | `FlatTree` 并行数组 + CSR |
| 排序 | `sorted_children()` 每次按 size 降序 | layout 前按 bytes 降序 | `childrenOf` + `sortEntries` |
| 变更 | `Tree.version` 递增，UI 缓存失效 | 会话结果 | `m_dataRevision` / `chartRevision` |

**结论**：布局算法可以直接吃 CSR（`childStart/childList`），不需要改成树。
我们缺的是“按 size 排序后的子项”缓存；可在布局入口临时排序（子项数量已受
`kChartChildren` 限制，成本可控）。

---

## 2. Squarified Treemap（两边核心相同）

### SquirrelDisk（`ui/treemap.rs`）
常量：`MIN_AREA=90 pt²`、`NEST_W=56`、`NEST_H=44`、`HEADER=17`、`PAD=3`、
`MAX_TILES=9000`。

每个父矩形内：
1. 子项已按 size 降序。
2. 逐个算 `a = area * child.size / parent.size`。
3. 若 `a < MIN_AREA` 且不是最后一个 → 从该项起把**尾部**合成一个 group，
   `rest = sum(tail)`；`group_area` 太小时整段省略。
4. 把可见项 + group 的面积归一化到矩形面积，`squarify` 切行。
5. 每个可见项得到 `(c0,c1)` 色相子区间：父区间按面积比例切分，
   `mid=(c0+c1)/2` 作为自己的颜色；嵌套时把 `(c0,c1)` 传给子层。
6. 只有 `rect >= NEST_W×NEST_H` 且 `depth < max_depth` 才继续嵌套；
   子矩形是 `inset(PAD, HEADER, PAD, PAD)`。
7. 超过 `MAX_TILES` 直接停。

`squarify` 的行判定：`worst(row) = max(side²·max/(sum²), sum²/(side²·min))`，
尝试加入下一个元素，worst 不再变小就收行。

### MangoDisk（`treemap-layout.ts` / `hierarchical-treemap-layout.ts`）
- 可见性按**比例 + 像素**双阈值：`minimumVisibleShare=0.0075`；视口已知时
  用 `600 px² / viewportArea`；小于阈值的尾部合成 **remainder**（条目数可空，
  不冒充完整统计）。
- `squarify` 的行判定和上面一致；面积严格按 bytes。
- 层级：`depth < depthLimit` 且 `width>=112 && height>=96` 才开 header
  （depth1 高 24，更深 18）；子层只占 header 以下的主体。
- 顶层区域 inset 1 px；子层共享父的边，避免缝隙随深度增长。
- 顶层 5 个主题色轮换（`index % 5`），子孙继承顶层 colorIndex。

### 我们的落地方案
在 `tree.h` 里新增（独立实现）：

```
struct LayoutTile {
  QRectF rect; int node; int depth; int parent;
  quint64 size; float hue0, hue1;
  int groupStart = -1, groupCount = 0; // 尾部聚合
  bool nested = false;
};
void layoutTreemap(TreeView, root, QRectF area, int maxDepth,
                   float h0, float h1, QVector<LayoutTile>& out);
```

伪码：

```
kids = sortedBySizeDesc(node)
areaTotal = area.w*area.h
shown=0; rest=0
for i,kid:
  a = areaTotal * size(kid)/size(node)
  if a<MIN_AREA && i+1<kids.size(): rest=sum(kids[i..]); break
  if a<MIN_AREA*0.25: break
  shown++
areas = first shown; if rest: areas += groupArea
scale areas to fill areaTotal; rects = squarify(areas, area)
acc=0
for k, r:
  frac = areas[k]/areaTotal
  c0 = h0+(h1-h0)*acc; c1 = h0+(h1-h0)*(acc+frac); acc+=frac
  if k==shown: out += Group{r, aggregateColor, count}
  else:
     out += Tile{r, kid, depth, color(hue=(c0+c1)/2, depth, isFile)}
     if nest(r, depth): layoutTreemap(kid, inset(r), depth+1, c0, c1, out)
```

与现状的差异：我们现在的 `treemapTiles` 没有按像素面积聚合、没有色相继承、
没有 header/inset 规则；这三项是“好看”的主要来源。

---

## 3. Sunburst

### SquirrelDisk（`ui/sunburst.rs`）
- 中心孔 `r0 = 0.2 * R`，环宽 `(R-r0)/rings`，rings 3–9。
- 角度按 size 比例；中环弧长 < `MIN_PX=4` 时把**尾部**合成 group；
  单独一个太小的项也省略。
- 颜色：`segment_color(angleMid/TAU, depth, isFile)`，即**绝对角度**在循环色轮
  上取色，再按深度调亮；与 treemap 的色相继承略有不同。
- 稳定 key = 节点 id / `group_key(parent, skip)`；视图切换时逐段从旧角度
  动画到新角度（750 ms）。
- `MAX_SEGMENTS=14000`，超出停止。

### MangoDisk（`sunburst-layout.ts`）
- 可见性：`(bytes/total)*span >= 0.015`（**1.5% 角度**）才画；省略的字节保留在
  父总量里，不重新分配。
- 只在 depth 1 加 remainder 扇区。
- 内半径 66、外 232；每段留 `gap = min(0.006, span/5)`。
- 标签宽度：`min(切向宽度, 角向宽度) - 12`，按字符宽度估算是否放得下。
- 顶层 5 色轮换、子孙继承。

### 我们的落地方案
- 采用 SquirrelDisk 的几何 + MangoDisk 的“角度阈值聚合”（1.5% 或 4 px 取大者）。
- 颜色用**绝对角度**色轮，和 treemap 的父区间色相在视觉上对齐。
- 环数默认 4，范围 3–9；中心孔 20%。
- 先实现静态布局，再考虑逐段动画。

---

## 4. 颜色模型（关键）

### SquirrelDisk（`ui/theme.rs`）
```
wheel_at(t): 在 8–11 个色相锚点间循环插值
segment_color(mid, depth, is_file):
  base = wheel_at(mid)
  (h,s,l) = HSL(base)
  d = depth-1
  l += d * depth_light        // 深色主题 +0.03/层，浅色主题可能 -0.02
  s *= 1 - d*0.025
  if is_file: l += 0.04*sign(depth_light); s *= 0.85
  clamp l 0.12..0.88
aggregate_color = 背景中点再压暗（深色 14%，浅色 7%）
```
- treemap 的色相不是绝对角度，而是父区间按面积切分 → 相邻块同色系。
- 目录/文件只靠明度与饱和度区分，不再按扩展名。

### MangoDisk
- 顶层 5 色轮换，子孙继承；深度只调 `opacity 0.8 + (depth-2)*0.025`。
- 更简单，但分支感强、相邻色块可能撞色。

### 建议
采用 SquirrelDisk 的“色轮 + 深度明暗 + 文件更亮更淡”，
矩形图用**色相区间继承**；聚合块用中性色。
扩展名颜色保留在图例（可选开关），默认不参与图表。

---

## 5. 聚合（remainder / smaller items）

| | 阈值 | 位置 | 可展开 |
|---|---|---|---|
| SquirrelDisk | 矩形 90 pt²、弧长 4 pt | 每个父级尾部 | 点击 group → 只显示被折叠项 |
| MangoDisk | 0.75% 或 600 px²/视口 | 尾部，depth1 的旭日也有 | remainder 列表 |

我们：第一阶段 group 只显示“较小项目 · N 项”，不可进入；
第二阶段给图表加 `View{node, skip}`（SquirrelDisk 的做法），
点击 group 只画被折叠的子项，再点中心返回。

---

## 6. 命中测试

- SquirrelDisk：矩形从后往前找包含点且 `depth>0` 的最深 tile；
  旭日用 `polar(point)` 得到 `(r,a)`，按环半径 + 角度定位。
- 我们已有 `hitTest`；需要补 group 的命中与“最深可见”规则。

---

## 7. 布局缓存

- SquirrelDisk 用签名 `(tree.version, view, depth, size, theme)` 缓存布局，
  只有签名变化才重算。
- 我们已有 `chartRevision`；把 `(revision, root, depth, viewMode, size, theme)`
  作为 `ChartItem` 的布局签名，避免 resize/悬停触发重算。

---

## 8. 扫描

### SquirrelDisk（`scan/local.rs`）
- 一组 worker 从共享 BFS 队列取目录；每处理完一个目录：
  `progress.files/bytes` 原子累加，错误单独计数。
- 硬链接：`(dev, inode)` 且 `nlink>1` 只计一次。
- 远程结果按 120 ms / 4096 条批量应用。
- 扫描中树是共享的，UI 直接画当前快照（“边扫边出图”）。

### MangoDisk
- 并行目录爬取 + 进度/取消；分析结果放后端会话，返回/前进不重扫。

### 我们
- 快扫 MFT 已经是单次读取 + 紧凑列；**补硬链接去重**：同一 MFT 记录的多条名字
  只计一次大小。
- CSR 的 `childrenOf` 是 O(1)，不需要目录缓存。
- 增量出图只对 < 50 万节点的扫描启用（每 1–2 秒推一次），C: 完成后出图。

---

## 9. 移植顺序（建议）

1. **布局与颜色**（无动画）：`layoutTreemap` / `layoutSunburst` 独立实现，
   色轮 + 深度明暗 + 像素聚合；与现有 `ChartItem` 对接。
2. **交互**：group 命中/返回、单击进入、中心返回、列表↔图表高亮同步。
3. **缓存与动画**：布局签名缓存；250 ms 角度/矩形过渡。
4. **扫描**：硬链接去重；必要时增量出图。
5. **可选**：扩展名配色开关、深度分别记忆。

## 10. 风险

- 聚合会隐藏小项：必须可查看（group 计数/展开），否则用户找不到文件。
- 动画与命中测试要在动画中间状态也正确（用目标布局做命中）。
- 色轮在浅色主题需要降低饱和度，否则刺眼。
- 我们的图表导出仍有 `QVariantMap` 开销；布局改成直接吃 CSR 后可去掉这层。

## 需要确认

1. 矩形图/旭日统一用 SquirrelDisk 的“色轮 + 深度明暗”，扩展名颜色只保留在图例？
2. group 第一阶段是否允许“不可进入、只显示数量”，第二阶段再做进入？
3. 是否现在就实现第 1 步（布局 + 颜色 + 聚合，无动画）？

---

## 11. 小文件阈值与内存

### 结论
有用，但**不能静默丢弃**：按目录聚合成一个“较小文件 (N 项)”伪节点，
总大小和文件数都保留，图表少掉几百万个节点；需要时再展开真实小文件。

### 为什么省
我们的节点成本约 40 字节 + 名字（UTF-16 + 4 字节偏移）。以 300 万节点、平均
名字 10 字符计约 80 字节/节点 ≈ 240 MB；若 70% 文件 < 64 KB，聚合约去掉
210 万节点 ≈ **170 MB**，布局、导出和列表也一起变小。

### 在哪一步做
- 快扫读 MFT 时：`appendBatch` 遇到文件且 `bytes < threshold`，不写入名字列，
  只累加到“父记录号 → (count, bytes)”的紧凑表；
- `buildFlat` 输出目录时，在正常子项之后追加一个“较小文件 (N)”子节点；
- 普通遍历同理：目录项读完后聚合。

### 阈值
设置项：关 / 16 KB / 64 KB / 256 KB / 1 MB（默认 64 KB 或关）。
只作用于**文件**，目录永远保留；聚合节点可点开，按需枚举该目录的真实小文件。

### 与扩展名图例
小文件仍要计入扩展名汇总（用聚合里的扩展名分布，或归入“其他”），
否则图例会少算空间。

## 12. 两个仓库的 MFT 与我们

| | 扫描方式 | 权限 | 结果保存 | 内存特点 |
|---|---|---|---|---|
| SquirrelDisk | Win32 `read_dir` + 元数据，worker 池 BFS | 普通用户 | 完整 `Vec<Node>` | 每节点一个 name + 链接字段 |
| MangoDisk | `FSCTL_QUERY_FILE_LAYOUT` 内核页式枚举；不可用时 Win32 遍历 | 卷句柄通常需管理员 | 目录聚合 + 有限层级投影 | 不保留全部文件节点 |
| 我们 | 直接解析 NTFS MFT 记录（$FILE_NAME/$DATA） | 宿主 elevated runner | 全部节点扁平化 + CSR | 名字打包，节点最全 |

差异要点：
- 两个参考项目都**没有自己解析 MFT**：SquirrelDisk 是目录遍历；MangoDisk 用
  内核 file-layout 枚举（同样是整卷元数据，但由内核分页返回）。
  MangoDisk 的 `windows/file_layout/parser.rs` 用的是
  `FSCTL_QUERY_FILE_LAYOUT`，不是原始 MFT 解析。
- 我们的快扫一遍读 MFT，速度最好，但保留全部节点；MangoDisk 只保留层级投影，
  所以内存小；SquirrelDisk 保留全部节点，但可以边扫边画。
- 硬链接：SquirrelDisk 按 `(dev,inode)` 去重；MangoDisk 用 file reference；
  我们目前同一 MFT 记录的多条名字会各建一个节点，**会重复计大小**，需要修
  （第一条计大小，其余计 0 或跳过）。
- 可选借鉴 MangoDisk 的“层级投影”：图表只保留 N 层 + 聚合，文件列表按需从
  磁盘或 MFT 结果取；这比小文件阈值更彻底，但要改模型接口。
