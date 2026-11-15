# 0.1.17 图标 / 复位 / 箭头 / 硬链接 / 小文件 / 图表配色

按约定分阶段提交：

1. `7869fe2` fix(qml): embed the exe icon, fill the chart and refresh the expand arrow
2. `fd3ce0e` fix(qml): count hard-linked files once in the fast scan
3. `fd979c1` feat(qml): fold small files into one row per directory
4. `de91a52` feat(qml): colour the chart by a hue wheel with depth shading

## 1. 图标
- 新增 `qmlapp/diskview-qml.rc`，把 `app.ico` 与 VERSIONINFO 编进独立程序和插件
  DLL；`main.cpp` 再设窗口图标。
- 检查：exe 的 `VersionInfo` 有版本，`ExtractAssociatedIcon` 返回 32×32 图标。

## 2. 复位后铺满
- 去掉 `ChartItem` 的 `anchors.margins: 8`；`resetView()` 无条件重排一次。
- 全屏截图：树图四边贴满。

## 3. 展开箭头
- `toggleExpanded` 的增量插入/删除后，对父行补发
  `dataChanged(row,row,{ExpandedRole})`，三角不再停在旧状态。

## 4. 硬链接
- 快扫同一 MFT 记录的多条名字只计一次大小；别名标记后不输出。
- 单测：两个名字同一记录 → 只输出一行，目录合计 100 字节。

## 5. 小文件聚合
- `ScanSettings` 新增“小文件忽略”：不忽略 / 16KB / 64KB / 256KB / 1MB
  （默认不忽略，避免改变现有结果）。
- 快扫在 `buildFlat` 里按目录把小文件折成“较小文件 (N 项)”一行：
  总大小、文件数保留；该行在图例里显示为“较小文件”，不进入文件列表。
- 单测：tiny 50B + big 200B、阈值 100 → 3 行（根、big、聚合），根合计 250，
  聚合 50/1 项。
- 注意：普通遍历（非 MFT）暂不聚合；快扫是 C: 的主路径。

## 6. 图表颜色（借鉴 SquirrelDisk）
- `tree.h`：treemap 增加色相区间继承（父区间按子块面积切分），子块颜色取区间
  中点；嵌套改为“顶部 12px 标题条 + 2px 边框”，不再是四周 10px。
- 旭日：扇区色相取绝对角度，环内按深度调亮/降饱和；小扇区按 4px 弧长裁剪。
- `ChartItem::segmentColor`：色轮 11 个色相锚点（红→橙→…→粉→红），
  只取色相；明度/饱和度固定并按深度调整：
  - 深色：L 0.50、S 0.62，每层 +0.03；
  - 浅色：L 0.62、S 0.52，每层 −0.015；
  - 文件再亮/暗一点、饱和 ×0.92；
  - 首个大块从蓝色开始（不是红色）。
- 扩展名颜色仍用于图例/文件列表的色点，不再决定图表颜色。

## 验证
- 扫描/模型/图表几何单测通过。
- `--self-test` 两种图表通过；Dock 16 次启动通过。
- 浅色树图/旭日截图：相邻大块同色系、层次可辨；深色主题沿用同一色轮。
- 未在真实宿主 C: 上验证小文件阈值的内存收益；需安装后观察。

## 未做（下一步）
- 矩形图按像素面积聚合（“较小项目”块）与点击展开。
- 色轮动画（250ms 过渡）。
- 普通遍历的小文件聚合。
