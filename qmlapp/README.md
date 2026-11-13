# Disk View QML 技术样机

本目录是 `ra9fael.diskview` 的 **QML 分支（`qml`）** 里的 Qt Quick 应用目录，与原生版共存于同一棵仓库树：原生 `main.cpp`、`viewer.h`、`scan.h`、`tree.h` 仍在分支根部，QML 代码集中在 `qmlapp/`。当前阶段先验证 Qt Quick 的数据模型与双图表渲染；固定快照仍作为默认数据源，真实扫描已接入。

## 当前状态

- 阶段 0：隔离目录与构建决策记录已建立。
- 阶段 1：独立启动的 wrapper（`diskview-qml.exe`）已完成，可脱离 WinShun 宿主单独运行、自检与截图。
- 阶段 2：真实扫描已接入（复用分支根部的 `scan.h`），固定快照仍作为默认数据源。
- 阶段 3：扫描设置（并发上限、介质策略、预览发布频率、排除路径、扫描方式）与扫描中的局部快照已实现。
- 阶段 4：虚拟化列表待实施。
- 阶段 5：Scene Graph 方块图与圆盘图待实施（当前用 `QQuickPaintedItem` 绘制）。
- 阶段 6：扫描控制已补齐——暂停/继续、用新设置重扫、NTFS MFT 快扫（管理员 helper）、扫描问题详情。
- 阶段 7：列表面板与导航已对齐原生版——按名称/大小排序、路径过滤、最大文件榜、面包屑、前进/后退/上一级、键盘导航、翻页。
- 阶段 8：图表交互补齐——绘图深度（1 层/2 层）可切换、方块与扇区带微型标签、悬停浮层、单击选中/双击进入、圆盘中心孔返回上级、只读右键菜单。
- 阶段 9：布局按 WizTree 式演示稿重排——上半文件夹树表 + 扩展名图例，下半方块图，面包屑夹在两者之间；方块按扩展名着色，多出「% 父级」「已分配」「项目/文件/文件夹」列。
- 阶段 10：界面的五处反馈（筛选框裁切、单击展开、列配置、图例列、设置对话框越界）。
- 阶段 11：B3 的性能测量与扁平数组传递（30 万节点导入 517 → 136 ms）。
- 阶段 12：接入 WinShun 宿主——无 Qt 的启动器 + 宿主 Qt 上的 DLL、插件协议、独立清单与打包脚本。

## 安全边界

- 不修改分支根部的 `plugin.json` 或原生 `diskview.exe`；QML 版的清单是 `qmlapp/plugin.json`，id 与原生版不同，两个插件并存安装、各读各的设置。
- 不进行真实文件操作；快照数据只用于可视化。
- 不启动新的插件或服务端。

## 构建与验证

构建必须在加载了 MSVC 环境的终端里进行，否则 CMake 的编译器探测会失败。QML 目标通过分支根部的开关启用，原生目标不受影响：

```
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B qmlapp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDISKVIEW_BUILD_QML=ON -DDISKVIEW_QT_PREFIX=D:\tools\Qt\6.12.0\msvc2022_64
cmake --build qmlapp/build
```

- `DISKVIEW_BUILD_QML=OFF`（默认）时不查找 Qt，原生版照常构建。
- `DISKVIEW_QT_PREFIX` 指向 Qt 6.12.0 MSVC SDK 根目录。
- `qmlapp/tests/check-prototype.ps1` 覆盖 fixture 递归求和、chart geometry 单元测试、扫描设置单元测试、扫描行为单元测试、列表模型单元测试、部署独立运行包、treemap/sunburst 双模式截图、可见节点命中与 zoom/pan 逆变换、模型目录进入/返回、真实扫描、扫描被按住的状态、helper 分派。检查期间的扫描读的是一份只含 `{"scanMode": 2}` 的临时设置，扫的是临时目录，不会弹 UAC。

## 接入 WinShun 宿主

`-DDISKVIEW_BUILD_PLUGIN=ON` 时额外产出宿主用的两段式插件（与 WinShun 自带的 `winshun.capture`、`ra9fael.binarylab-qml` 同一套做法）：

- `diskview-qml.exe`：无 Qt、静态 CRT 的启动器（`qmlapp/stub/main.cpp`），读 `WS_APP_DIR`、把宿主的文件夹加进 DLL 搜索路径，加载同目录的 `DiskViewQmlApp.dll`；
- `DiskViewQmlApp.dll`：跑在 Qt 上（`qmlapp/src/plugin_main.cpp`），`QGuiApplication` 留在主线程，`winshun::Plugin::serve()` 在工作线程，`query`/`action`/`appearance`/`shutdown` 都接上了；窗口就是现有的 `QmlHost`。

产物在 `build-plugin/plugin/`。`tools/package-qml.py` 打包成 `dist/ra9fael.diskview.qml-<版本>.wsplugin`，`tools/verify/smoke-plugin-qml.py` 用真实协议验证（查询、打开、扫描、主题、关闭）。

**为什么随包带 Qt**：宿主的 Qt 只承诺 Core/Gui/Qml/Quick 与它自己那套扁平控件，`QtQuick/Controls` 没有顶层 `qmldir`（模块在 `Qt6QuickControls2.dll` 的资源里），而这个窗口用了 `QtQuick.Controls`。所以清单里**不写** `runtime.qt`/`qtVersion`（这是宿主表达「自带 Qt」的方式），包内附 SDK 的 Qt，`windeployqt --qmldir` 按实际 import 部署，再裁掉没用到的模块。代价是包约 97 MB（宿主上限 256 MB）。

**已知取舍**：设置文件用 `QFile` 直写而不是 `QSaveFile`——后者在这种部署下写同目录临时文件再改名会被拒（"Access is denied"），为此放弃了原子写。超大批量设置的极端情况可能留下半截文件，重开即恢复默认。

## 独立运行包

`qmlapp/tools/deploy-wrapper.ps1` 用 `windeployqt` 把 Qt 运行时拷到 `qmlapp/build/deploy`，该目录自带 `plugins/`、`qml/` 与 `qt.conf`，**不依赖 Qt SDK 也不依赖 PATH 里的任何 Qt 变量**，可直接拷走运行：

```
powershell -File qmlapp/tools/deploy-wrapper.ps1
qmlapp\build\deploy\diskview-qml.exe
```

wrapper 的命令行：

| 参数 | 说明 |
| --- | --- |
| `--view-mode <0\|1>` | 0 = 方块图（treemap），1 = 圆盘图（sunburst） |
| `--fixture <路径>` | 覆盖默认快照 `:/fixtures/disk-tree.json` |
| `--scan <路径>` | 启动即扫描该目录，用真实扫描结果替换快照 |
| `--qml-dir <目录>` | 加载目录里的 `Main.qml`，用于原型试验 |
| `--screenshot <路径>` | 延时后抓图并退出 |
| `--delay <毫秒>` | 抓图前的等待时间，默认 1200 |
| `--settings` | 启动即打开扫描设置对话框 |
| `--pause` | 启动即扫描并把扫描按住不放，用于看「扫描已暂停」的样子 |
| `--self-test` | 跑命中/变换/导航自检并打印制表符分隔的报告 |
| `--bench-chart <节点数>` | 造一棵该规模的树，量图表全链路耗时并退出 |
| `--help` | 用法 |

wrapper 的可执行文件同时是管理员快扫 helper：`--scan-helper <盘符> <管道名> <调用方 pid>` 会被 scan session 以 `runas` 提权启动，这条命令在 GUI 之前接管，退出码 2..7 表示不同的失败原因。

自检报告形如 `0<TAB>2<TAB>regions<TAB>transform<TAB>navigate<TAB>return<TAB>list=sort,name,filter,files,expand,history<TAB>drag<TAB>chart=double<TAB>PASS`，末列 `PASS` 表示全部通过。带 `--scan` 时还会多一行 `scan<TAB>state=…<TAB>mode=…<TAB>workers=…<TAB>excluded=…<TAB>partials=…<TAB>files=…<TAB>dirs=…<TAB>skipped=…<TAB>pause=…`。`pause=verified` 表示这次扫描确实被按住过（按住期间模型读到 0 项），`mode=fast` 表示走的是 MFT 快扫。

## 真实扫描

`ScanService` 复用分支根部的 `scan.h`（连同 `tree.h` 与 SDK 头 `winshun.h`），在工作线程上跑 `diskview::scanPath`，把结果转成与快照同形的树交给 `DiskViewModel::loadTree`。窗口顶栏有路径框、扫描与取消按钮，底栏显示文件数、目录数与耗时。

- 扫描不跟随符号链接/junction，不打开文件内容，云端占位文件不会被下载。
- 扫描与界面解耦：进度、取消和最终结果都通过队列回到 GUI 线程。
- 命令行 `--scan <路径>` 走的是同一条路径，`--self-test` 会额外验证扫描结果能进模型、能命中、能进目录再退回。

## 扫描设置

`ScanSettings` 把设置存在 `diskview-qml.json`（位置取 `WS_DATA_DIR`，否则 `QStandardPaths::AppDataLocation`）。文件名与原生插件的 `diskview.json` 刻意不同，因为两个插件要并存安装，各读各的。

| 设置 | 取值 | 说明 |
| --- | --- | --- |
| 扫描方式 | 自动 / 快扫（MFT，需管理员）/ 普通遍历 | 自动 = 能用 MFT 就用，不能就退回普通遍历 |
| 并发上限 | 自动 / 1 / 2 / 4 / 8 | 自动时由扫描器决定；填别的数会吸附到最近的一档 |
| 介质策略 | 自动检测 / SSD / 机械硬盘 / 网络目录 | 传给扫描器的 `ScanOptions::Media` |
| 预览发布 | 平衡（1.5s）/ 更少更新（5s） | 局部快照的发布间隔 |
| 排除路径 | 路径列表 | 扫描时跳过的目录 |

设置只影响下一次扫描，当前结果不变。对话框是草稿式的：改动先落在对话框的副本上，点「确定」才写回，点「取消」什么都不改。对话框里有一个「确定后立即重新扫描」勾选项，勾上就当场用新设置重扫一次。

## 扫描控制

- **暂停 / 继续**：扫描中时顶栏出现「暂停」，按住后扫描器一个字节也不读（引擎级的测试证明这一点），界面标注「扫描已暂停 · 点「继续」让它接着扫」。取消时会把按住状态一并放开，否则被按住的扫描看不到取消。
- **重新扫描**：扫描结束或被取消后按钮变成「重新扫描」，用当前设置重跑一遍。
- **快扫**：只有固定 NTFS 卷的盘符根目录才够格走 MFT 快扫。快扫要管理员权限，wrapper 用 `ShellExecuteExW` 的 `runas` 把**自己**再启动一遍当 helper，两边通过 `\\.\pipe\DiskView.Scan.<guid>` 管道传数据。UAC 被拒或 helper 失败时自动退回普通遍历，并在底栏和「扫描问题」对话框里说明退回原因。
- **扫描问题**：读不到的目录、跳过的链接、大小未知的文件都记在 `ScanResult` 里，界面上是「扫描问题」按钮（只在有问题时出现），点开列出具体路径和 Windows 错误码对应的说明。

## 扫描问题详情

`ScanIssuesDialog.qml` 列出这次扫描里没能读到的目录及原因、跳过链接数、大小未知的文件数，以及快扫退回普通遍历的理由。目录被独占打开（权限不足、文件被其他程序锁住）时也会出现在这份清单里，附上 `FormatMessageW` 取到的系统错误文本。

## 界面布局

竖向五段，参考 `design/wiztree-layout-prototype.html` 演示稿：

1. 标题行（状态灯）
2. 扫描行（路径框 + 扫描 / 暂停 / 取消 / 扫描问题 / 扫描设置）
3. 视图行（树视图 / 文件视图 + 筛选框）
4. 上半分栏：文件夹树表 | 扩展名图例（可拖竖分隔条，上下也可拖）
5. 导航行：← → ↑ + 面包屑 + 「当前目录 · 大小 · N 项」
6. 方块图
7. 状态栏：选中项、扫描状态、悬停路径

面包屑放在表格与方块图之间，因为它描述的是**下方方块图画的是哪一层**；表格是独立的数据视图，有自己的展开状态。

## 文件夹树表

- **树视图**：扁平化的树，`▸/▾` 展开折叠，缩进随层级；**单击文件夹行即展开/收起**，双击进入目录，小三角也还在。列是「文件夹 / 名称 | % 父级 | 大小 | 已分配 | 项目 | 文件 | 文件夹 | 修改时间 | 属性」。% 父级是一条内嵌条加数值，相对**父目录**而不是视图根。
- **文件视图**：当前子树的全部文件（可过滤），按当前排序键排列，没有层级缩进。两个开关：**范围**（当前目录 / 整个扫描）与**匹配方式**（子串 / 模糊子序列）。
- **列配置**：表头上方「列」菜单逐列勾选，选择写进 `diskview-qml.json` 的 `hiddenColumns`，下次启动保持；名称列不可隐藏。
- **修改时间 / 属性**：两条扫描路径都把 `ftLastWriteTime` / `FILE_ATTRIBUTE_*` 带进节点（MFT 从 `$STANDARD_INFORMATION`，普通枚举从 `WIN32_FIND_DATA`）；属性按 Explorer 的字母显示（R H S D A L C）。固定快照没有这些字段，显示「—」。
- **排序**：点「名称」「大小」「修改时间」表头排序，同列再点翻转方向，换列取自然方向（名称升序、大小降序）。展平时逐层排序。
- **过滤**：输入即过滤，树视图匹配整条路径（大小写不敏感）；命中的行连同它上方的路径一起显示，筛选期间路径上的文件夹临时展开，清空后恢复用户自己的展开状态。
- **已分配**：扫描结果自带 `allocApprox`；固定快照没有这个字段，按 4 KB 簇向上取整。
- **导航/键盘**：双击行或回车进入目录，`↑`/`↓` 移动选择，`←`/退格上一级，`Alt+←`/`Alt+→` 前进后退，`Ctrl+F` 过滤框，`Ctrl+L` 路径框，`F5` 重扫，`Esc` 清空过滤。

## 扩展名图例

右侧图例按扩展名聚合当前子树：颜色块、扩展名与类型名、**占比条**、大小、已分配、文件数，按大小降序。**点一行**会把方块图里其它扩展名压暗，再点一次恢复；颜色表只有一张（`src/ExtensionColors.h`），图表和图例取同一个函数，所以颜色永远对得上。

## 性能（B3 的测量结果）

`--bench-chart <节点数>` 造一棵该规模的树并量全链路：模型导入、`chartTree()` 导出、图表解析、布局、绘制。1280×840、软件渲染、Release：

| 节点数 | 模型导入（扁平） | 模型导入（嵌套） | 导出 | 图表解析 | 布局 | 绘制 |
| --- | --- | --- | --- | --- | --- | --- |
| 5 万 | 22 ms | 93 ms | 2.6 ms | 3.5 ms | 0.00 ms | 0.45 ms |
| 30 万 | 136 ms | 492 ms | 3.3 ms | 3.8 ms | 0.06 ms | 0.31 ms |

（最初的数字：导入 2129 ms、导出 543 ms、解析 868 ms。）

结论：

- **布局与绘制不是瓶颈**：方块按面积裁剪，30 万节点时实际只画十几个块，1 层/2 层都不到 1 ms。所以**没有**改 `QSGGeometryNode`——那是给一个不存在的瓶颈做优化，而且 QSG 路径在软件/无头渲染下还得再维护一套回退。
- **图表只收画得出的层数和大块**：`chartTree()` 只导出 3 层、每层最多 256 个子项（按大小排序），深度 2 的细碎方块本来也会被面积裁剪掉。
- **扫描结果改成扁平数组**：`ScanService` 不再把树展开成嵌套 `QVariantMap`（每节点一次哈希查找、每个子项一次 map 拷贝），而是在工作线程上填 `FlatTree`（并行数组：名称、父索引、大小、已分配、时间、属性、计数、标志），模型 `loadFlat()` 一趟读进来。30 万节点 517 → 136 ms。微基准：光遍历 30 万节点的 `QVariantMap` 就要 145 ms，路径拼接只占 6 ms。
- **节点的 id 不再存**：id 就是「位置 + 1」，`indexForId` 直接解析数字，30 万个 QString 的分配省掉了。
- 圆盘扇区改用真正的圆弧（`arcTo`），不是折线近似，缩放时边缘不再有棱角。

## 图表交互

- **着色**：文件按扩展名着色，文件夹用中性灰，图例被选中时其余文件变灰。之前是按层级着色（同层同色）。
- **绘图深度**：「1 层 / 2 层」控制图表画几层，对应原生 `s.depth`。默认 1 层——方块图里父 tile 会被子 tile 完全盖住，画得越深越点不到目录，1 层才让每个目录成为可双击的整块。
- **标签**：按原生规则画微型标签——方块 ≥48×14 画名称、≥32 高再补大小；扇区张角 >0.35 弧度且环宽 >26px 才画。文字颜色按底色亮度取黑或白。
- **悬停**：鼠标下的区域加深并描边，同时弹出浮层（名称 / 完整路径 / 文件夹|文件 · 大小 · 占比 / 操作提示）。浮层不拦截鼠标，移出图表即消失。
- **单击/双击**：单击选中（不再像之前那样悬停即选中），双击目录进入；圆盘中心孔内双击返回上一级（判定半径用中心孔半径，不是写死的 24px）。
- **拖动**：只有按住左键并移动超过 3px 才平移，松开即结束——之前缺 `mouseReleaseEvent`，一次点击之后图表会一直跟着鼠标跑。
- **右键**：只读菜单——复制完整路径、在资源管理器中显示、定位到所在目录；文件操作按计划留到宿主集成阶段。

## 局部快照

扫描过程中，扫描器每隔一段时间把当前已经读到的树发回来一次，界面随即刷新，不必等整个目录读完。底栏的来源标签会区分「扫描中」「已取消的扫描」「扫描失败」和「固定快照」；被取消的扫描仍会显示它已经读到的那部分，并标注「部分结果 · 扫描未完成，数据仍在变化」。

## 三个环境坑

- **必须用动态 CRT**：Qt 官方 DLL 是 `/MD` 编译的，静态 CRT（`/MT`）的 exe 与 Qt DLL 不共享堆，QML 类型注册跨堆写入时会触发堆断言。CMake 里固定为 `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`。
- **offscreen 平台没有字体**：offscreen 用的是 FreeType 字体库，不设 `QT_QPA_FONTDIR` 时所有文字都是空方框。wrapper 在 `QT_QPA_PLATFORM=offscreen`/`minimal` 时自动把 `QT_QPA_FONTDIR` 指向 `C:\Windows\Fonts`。
- **offscreen 的通用字族名不可信**：那套 FreeType 字体库把通用的 "Sans Serif" 解析成了 Agency Gothic CT，拉丁字母的字形是错的（"QML" 显示成 "D4"）。wrapper 在 offscreen/minimal 下把应用字体显式定为 Segoe UI，并留 Microsoft YaHei 等中文字体兜底。原生（windows/D3D11）路径不受影响。

详见 [迁移执行记录](design/migration-status.md)。
