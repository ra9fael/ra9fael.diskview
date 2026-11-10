# 精简布局：原生落地记录

日期：2026-11-09。对应原型：`design/compact-layout-prototype.html`；
原型说明：`design/compact-layout-status.md`。

本轮把原型的精简布局落到原生 Win32 / GDI 界面，改的是 `viewer.h` 的布局与绘制，
以及 `viewer_commands.h` 的命令表。扫描、MFT、回收站、Direct2D 圆盘等既有实现未改动。

## 布局变化

改造前是 5 行工具条加整块详情面板，图表可用高度约 60%。改造后是：

| 行 | 内容 |
| --- | --- |
| 标题行（32px） | 品牌、操作、快捷键、深色 |
| 导航行（34px） | 后退 / 前进 / 上级、面包屑、选择磁盘、重新扫描 |
| 图表工具条（30px） | ↑ 返回父级、当前目录与大小、1/2 层、方块 / 圆盘 |
| 内容区 | 图表 + 分隔条 + 列表，图表填满自己的窗格 |
| 状态栏（44px，两行） | 第一行选中项名称 / 路径 / 大小占比 / 进入·定位·更多；第二行读取问题、快捷键提示、图表像素占比 |

1184×761 实测图表高度 599px，占窗口 79%。原型里的“图表占窗口”读数现在由原生状态栏
自己显示，不再需要对比旧布局来估算。

## 具体改动

- `computeLayout` 重写：新增 `rcTitle`、`rcNav`、`rcChartBar`、`rcHeading`、`rcStatusKeys`、
  `rcBudget`、`rcIssues`；`rcSource`、`rcOverview`、`rcSkipped`、`rcAccounting`、`rcDetail`
  置空保留（结构体字段仍在，避免大范围改动调用点）。
- 图表窗格内不再有标题条、缩放按钮、平移按钮：`rcZoomIn`/`rcZoomOut`/`rcFit`/`rcZoomLabel`/
  `rcPanUp`/`rcPanDown`/`rcPanLeft`/`rcPanRight`/`rcPanHome` 全部置空。滚轮、空格拖动、
  中键拖动、`0` 键、`+`/`-` 覆盖同样的能力。
- `paintAll` 按新行结构绘制；`drawStatusRows` 重写为两行状态栏。状态栏第一行在根目录时
  不重复显示与名称相同的路径。
- `activateAt` 移除缩放 / 平移按钮分支，读取问题改为点击状态栏的计数打开非模态面板。
- 命令表新增 `Command::Ancestors`，`showAncestors` 从独立按钮改为菜单与 Ctrl+K 入口，
  弹出位置改到父级按钮下方。
- 窄窗口（< 760px）图表工具条分两行：父级按钮与当前目录一行，视图切换一行；状态栏
  同样分两行堆叠。窄窗口下不再有横向溢出。

## 验收

`ctest --test-dir build` 两项全过（`diskview_logic`、`diskview_protocol`）。

新增与调整的断言：

- 图表高度占窗口 ≥ 72%（原来约 60%），并检查工具条在图表之上、状态栏贴住窗口底部。
- 图表窗格内不再有缩放 / 平移按钮（`rcZoomIn`、`rcPanUp` 为空矩形）。
- 窄窗口下列表底部在状态栏之上，图表在工具条与状态栏之间。

原先针对 `rcDetail` 与 `rcFit`/`rcPanUp` 的三条断言测的是被本轮删除的旧布局，已改为
测新布局的对应不变量。

截图：`build/verify-compact/`（离屏渲染，`DISKVIEW_SNAPSHOT_DIR` 输出 BMP，转 PNG 查看）
与 `build/compact-live*-printwindow.png`（真实 `--view` 窗口，PrintWindow 抓取）。

## 边界

- 本轮只改布局与绘制，未改扫描引擎、MFT 读取、回收站实现或 Direct2D 圆盘几何。
- 真实窗口验证使用本机 `--view` 入口和 PrintWindow 抓取；屏幕抓取在窗口被遮挡时是空白，
  因此以 PrintWindow 结果为准。
- 未做 100% / 125% / 150% / 200% 系统缩放的逐档人工核对；现有 96 / 120 / 144 / 192 DPI
  截图由测试生成，布局按 `S()` 缩放，未发现溢出。
- 未替换宿主已安装版本，未发布、未提交版本库。
