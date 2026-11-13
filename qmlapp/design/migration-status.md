# Disk View QML 迁移执行记录

## 决策

- 原生 `ra9fael.diskview` 保持为正式版和回退基线；QML 原型独立放在 `ra9fael.diskview-qml/`。
- 先验证数据与图表，不同时重写扫描器或改变正式插件清单。
- C++ 继续拥有磁盘树、字节数、路径、布局和命令执行；QML 只消费稳定 ID、可见项和展示用数值。
- 正式图表采用自定义 Qt Quick `QQuickItem` / Scene Graph，避免每个文件一个 QML delegate。样机阶段可以先选择容易调试的 renderer，但性能验收必须覆盖最终路径。
- QML 插件应沿用 WinShun 的 host Qt 契约和 `WS_APP_DIR` DLL 搜索策略；轻量启动器不加载 Qt。正式 helper 仍应保持原生、短生命周期，不能依赖 GUI Qt 初始化。
- 文件操作后续继续由 C++ 服务执行；技术样机不做真实文件操作。

## 环境调查（2026-11-11）

- 现有 QML 插件 `ra9fael.binarylab-qml` 提供可参考实现：轻量 exe 加载插件目录 DLL、`WS_APP_DIR` 注入宿主 Qt 路径、GUI 主线程 + 协议工作线程，以及 Qt 6.12.0 host runtime 清单声明。
- 当前正式 DiskView 的 `tree.h` 是纯 C++，包含 `Node`、treemap 布局和 sunburst 扇区算法，具备被 Qt 适配层复用的条件。
- `scan.h` 使用 `ScanResult` / `ScanProgress` / `ScanOptions`，但直接包含 `winshun.h` 与 Windows API。当前仓库的 `vendor/winshun/` 仅含 NTFS reader，不含完整插件 SDK 头，因此不能将 scanner 原样搬到独立 QML 工程。
- 本机 `C:\Program Files\WinShun` 有 Qt runtime DLL 与 `qml/QtQuick` 模块。
- 从 `ra9fael.hello/build/CMakeCache.txt`、`build/binarylab-variants-20261109/qml/CMakeCache.txt` 等已生成配置中找到匹配 SDK：`D:/tools/Qt/6.12.0/msvc2022_64`，Qt 6.12.0 MSVC 2022 x64。接下来需核对宿主 DLL 版本/ABI 并完成最小加载验证。

## 阶段状态

### 阶段 0：隔离

已创建本目录；不改正式 `plugin.json`、exe、安装包。本目录现已纳入 `ra9fael.diskview` 仓库的 `qml` 分支，并作为该分支的 worktree 检出在 `ra9fael.diskview-qml/`；原生代码留在分支根部，QML 代码集中在 `qmlapp/`。

### 阶段 1：Qt 生命周期

已定位 Qt 6.12.0 MSVC SDK，并以隔离运行目录验证 Qt Quick 窗口、QML imports、Basic Controls style、字体加载和 offscreen 生命周期。`DISKVIEW_QML_DIR` 与 `DISKVIEW_SCREENSHOT` 支持开发预览和自动截图。正式 host Qt 启动器尚未接入 WinShun 协议。

在加载 MSVC 环境的终端里（`vcvars64.bat` + Ninja），CMake 配置与构建均已跑通，产出 `build-cmake/bin/diskview-qml-prototype.exe`。此前记录为「ABI try-compile 会挂起」的现象，实际原因是构建命令没有在 MSVC 环境下执行。

按用户要求「先做一个独立启动的 wrapper 来验证功能」，QML 侧已有一支控制台子系统的启动器 `diskview-qml`（`qmlapp/src/QmlHost.{h,cpp}`）：不经过 WinShun 宿主即可加载 QML、切换视图模式、跑自检、抓图。`qmlapp/tools/deploy-wrapper.ps1` 再用 `windeployqt` 生成 `qmlapp/build/deploy` 运行包，该目录自带 `plugins/`、`qml/` 与 `qt.conf`，脱离 Qt SDK 与 PATH 也能跑。

### 阶段 2：快照与扫描接口

优先把 `tree.h` 保持为 C++ 核心；需要先拆分或适配 `scan.h` 对 `winshun.h` 的依赖，且不能让 QML 持有裸 `Node*` 跨快照。

已接入真实扫描：`ScanService` 在工作线程上跑分支根部的 `diskview::scanPath`（`scan.h` + `tree.h` + 仓库内的 SDK 头 `winshun.h`），把 `Node` 树转成与快照同形的 `QVariantMap`，再交给 `DiskViewModel::loadTree`。窗口顶栏有路径框、扫描与取消按钮，底栏显示文件数、目录数与耗时；命令行 `--scan <路径>` 走同一条路径。扫描不跟随 reparse point、不打开文件内容。

未接入 WinShun 协议与文件操作；MFT 快扫当时未接入（`mft_scan.h` 依赖 `vendor/winshun/Ntfs.cpp` 的 NTFS reader），已在阶段 6 补上。

### 阶段 3：列表模型

已完成固定 JSON 快照的 `DiskViewModel`：C++ 持有节点、稳定 ID、父子关系、大小格式化和当前目录；QML 使用 `QAbstractListModel` + `ListView`，截图已验证当前层显示 5 项。随后阶段 2 的真实 `ScanResult` 也用同一套模型承载。

### 阶段 4：图表样机

已把正式 `tree.h` 的 `treemapTiles` 与 `sunburstSlices` 接入 ChartItem；treemap/sunburst 共享缓存几何和命中坐标，支持滚轮缩放、拖动平移、悬停选择、双击目录进入。圆盘中心显示当前目录名，双击中心返回父级。绘制使用 `QQuickPaintedItem`/QPainterPath；先前 QSGGeometryNode 在当前 software/offscreen 环境没有呈现，故暂用可靠适配器，性能路径尚未验收。

### 阶段 5：扫描设置与局部快照

按「先补体验类功能」的决定，先把扫描设置与局部快照做掉，再回头补 MFT 快扫等内核能力。

- `ScanSettings`（`qmlapp/src/ScanSettings.{h,cpp}`）把并发上限、介质策略、预览发布间隔、排除路径和上次扫描路径存在 `diskview-qml.json`。文件名与原生插件的 `diskview.json` 刻意分开：两个插件要并存安装，各读各的设置。并发值超出 `{0,1,2,4,8}` 时吸附到最近的一档，保证设置永远是扫描器认得的数。
- `ScanSettingsDialog.qml` 是草稿式对话框：改动先落在对话框副本上，「确定」才写回设置并保存，「取消」什么都不改（第一版直接在控件变更时写内存设置，取消后残留改动，已改掉）。
- `ScanService` 新增 `started`/`partialTree` 信号与 `partialCount`/`scanWorkers`/`excludedItems` 属性。扫描器每隔 `previewIntervalMs` 把已读到的树发回界面，长目录不必等读完才有画面。取消时仍把已读到的部分作为「部分结果」交给模型，并在底栏标注「扫描未完成，数据仍在变化」。
- `QmlHost` 在设置载入后才设 context property，`initialScanPath` 因此可以在命令行没给路径时回落到上次扫描的路径；`--settings` 让 wrapper 启动即打开设置对话框，便于截图核对。
- 状态标签区分「真实扫描 / 扫描中 / 已取消的扫描 / 扫描失败 / 固定快照」，失败时底栏直接显示错误文本。

### 阶段 6：扫描控制（MFT 快扫、暂停/继续、扫描问题）

补齐 B1：内核能力全部复用分支根部已有的东西，QML 侧只做接线。`parallel_scan.h` 本来就有 `ScanOptions::paused`（`const std::atomic<bool>*`，工作线程里的 `waitPause()` 与派发循环都看它），`scan_helper.h` 本来就有提权 helper 的管道协议，根部的 `main.cpp` 本来就有 `--scan-helper` 分派。

- **暂停/继续**：`ScanService` 持有 `std::atomic<bool> m_pause`，`pause()`/`resume()` 通过 `stateChanged` 通知界面；`cancel()` 必须先把按住状态放开，否则被按住的扫描看不到取消。wrapper 的自检会先按住 400 ms，确认模型一项都没读到（`pause=verified`），再放开跑完交互检查——这条断言不靠人眼。
- **快扫**：`ScanSettings` 增加 `scanMode`（0 自动 / 1 快扫 / 2 普通遍历），与原生 `ViewConfig` 的 `scanMode` 同义。`ScanService` 在盘符根目录且 `mft::eligible()` 成立时用 `helper::scan()` 走提权 helper；`Outcome::Fallback`（UAC 被拒、helper 失败）自动退回普通遍历并把原因记进 `fallbackReason`。helper 用 `ShellExecuteExW` 的 `runas` 启动**同一个 exe**，管道 `\\.\pipe\DiskView.Scan.<guid>` 传数据；`main.cpp` 在任何 GUI 之前拦截 `--scan-helper`，所以它是个短命的纯读取进程。UAC 的窗口句柄由 `QmlHost` 把 `QQuickWindow::winId()` 交给 `ScanService`。
- **扫描问题**：`ScanResult` 的 `skippedList`（最多 32 条 `{path,error}`）、`skippedDirs`、`skippedLinks`、`unknownSizes` 被整理成 `issues`（`{path, code, message}`），错误文本用 `FormatMessageW` 取。`ScanIssuesDialog.qml` 在有问题时以「扫描问题」按钮出现。
- **用新设置重扫**：设置对话框加「确定后立即重新扫描」勾选项，接受时调 `ScanService::rescan()`；顶栏的按钮在扫描结束后变成「重新扫描」。
- 顺带修掉一个既有缺陷：扫描线程原先直接读 `m_settings` 的成员，而设置对话框可能在 GUI 线程上同时改它们。现在在 GUI 线程里读一次设置、按值拷进 `ScanOptions` 再交给线程。

## 验证结果（阶段 3–5）
- Qt SDK：`D:/tools/Qt/6.12.0/msvc2022_64`；host DLL：Qt 6.12.0.0；MSVC 19.51.36260。
- CMake 配置与构建通过（`vcvars64.bat` + Ninja，MSVC 19.51.36260），产物 `qmlapp/build/bin/diskview-qml.exe`；早前的手工 `moc` + `cl` + `link` 链路已由 CMake 取代。QML 目标挂在分支根部 CMake 的 `DISKVIEW_BUILD_QML` 开关后面，关闭时不查找 Qt，原生目标不受影响。
- `tests/check-prototype.ps1` 通过：递归 fixture 总和校验、C++ chart geometry tests、扫描设置单元测试（25 项断言）、部署独立运行包、两种模式截图、可见节点 hit-test、zoom/pan inverse transform、DiskViewModel 目录进入/返回、**真实扫描用例**（脚本先写出一棵小树，再 `--scan` 扫描它并验证命中/变换/进出目录）。treemap 截图 `build/qml-treemap-native.png` 约 40 KB（mode 0 命中 ID `8`）；sunburst 截图 `build/qml-sunburst-painted.png` 约 87 KB（mode 1 命中 ID `2`）；扫描截图 `build/qml-scan-real.png` 约 41 KB。检查跑的是部署后的副本，且已把 Qt SDK 从 PATH 里摘掉。
- 设置确实作用到了扫描：把 `qmlapp/build` 加进排除路径后，自检报告的 `excluded=1`，文件数从 1816 降到 224；并发设 1 被采纳（默认自动时是 4）。扫描 `D:/` 用时 97 秒，报告 `partials=8`，局部快照确实在扫描中途发布过。
- 三个环境坑及其结论：
  - **静态 CRT 会崩**：目标原先用 `/MTd`，与官方 `/MD` 的 Qt DLL 不共享堆，QML 类型注册的静态初始化跨堆写入时触发 `__acrt_first_block == header` 堆断言（进程卡在弹窗上）。改为 `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` 后 Debug/Release 均正常。
  - **offscreen 没有字体**：offscreen 走 FreeType 字体库且 Qt 6 不再自带字体目录，不设 `QT_QPA_FONTDIR` 时所有字形都是空方框（截图只有 8 KB）。wrapper 在 offscreen/minimal 平台自动把 `QT_QPA_FONTDIR` 指向 `C:\Windows\Fonts`，字形恢复正常。
  - **offscreen 的通用字族名不可信**：上面那条补上字体目录之后，拉丁字母仍然是错的（"QML" 显示成 "D4"、"GB" 显示成 "BB"，所有字母都偏大写）。用一支字体探针程序（`build/fontprobe/`，不入库）逐个渲染后确认：这套 FreeType 字体库把通用的 "Sans Serif" 解析成了 Agency Gothic CT。改成在 offscreen/minimal 下显式把应用字体定为 Segoe UI（中文回落到 Microsoft YaHei 等）后字形正确。原生 windows/D3D11 路径一直是对的，此坑只在无头验证路径上出现。
- 发现 fixture 的 `WinShun/build/debug` 子项总和比声明多 1 MiB，已修复样机 JSON 并加入 `tests/check-fixture.cjs` 递归检查。截图中的未填充扇区代表叶文件没有下一层子项，并非绘制断裂。
- HTML 交互演示位于 `../ra9fael.diskview/design/compact-layout-prototype.html`，提示文案已区分浏览器仿真与 Qt 实测，并提供 Qt 截图及 QML 源码链接。`node build/compact-prototype-check.cjs`：26 项全部通过。

### 阶段 9：按 WizTree 式布局重排

用户给了 WizTree 的截图并认可一张 HTML 演示稿（`design/wiztree-layout-prototype.html`），要求照着同步到 QML。

- **布局**：上下分栏（`SplitView`）——上半「文件夹树表 | 扩展名图例」，下半方块图；面包屑单独一行夹在两者之间（它描述的是图表画的是哪一层），并带当前目录的大小与项目数。
- **树表**：`DiskViewModel` 从「当前层扁平列表」改成「扁平化树」。新增 `depth`/`hasChildren`/`expanded`/`items`/`files`/`dirs`/`ext`/`extColor` 角色和 `toggleExpanded`；`% 父级` 改为相对父目录；`已分配` 取扫描的 `allocApprox`（快照没有就按 4 KB 取整）。文件视图取代原来的「最大文件榜」。
- **过滤的语义**：命中行连同它上方的路径一起显示；筛选期间路径上的文件夹**临时**展开，不写进用户的展开状态，清空后完全恢复。
- **扩展名着色**：新增 `src/ExtensionColors.h`，颜色表、类型名、取扩展名三件事只此一处；`ChartItem` 用它给方块/扇区上色，图例用它显示同色块。点图例某行会把其它扩展名压暗（`highlightExtension`）。
- 目录保持中性灰，和 WizTree 一致；因为绘图深度默认 1 层，进入某个目录后满屏都是按扩展名着色的文件块。

### 阶段 11：B3 性能（先测量，后动手）

B3 原本的计划是「QSGGeometryNode 渲染路径 + 抗锯齿圆盘 + 大目录压力」。先做了压力测量，结论推翻了其中一项：

- **测量工具**：`--bench-chart <节点数>` 造一棵 12 个顶层目录、每目录平铺文件的树，分别量模型导入（`loadTree`）、导出（`chartTree`）、图表解析（`setTreeData`）、布局（`rebuildGeometry`）、绘制（`paint` 到 QImage），1 层与 2 层各跑 3 次取均值。
- **优化前**（30 万节点）：导入 2129 ms、导出 543 ms、解析 868 ms、布局 0.00 ms、绘制 0.48 ms。
- **根因**：图表每次导航都会收到整棵子树的 `QVariantMap` 深拷贝（再解析成第二棵 `diskview::Node`）。而布局与绘制是常数——方块按面积裁剪，30 万节点时真正画出来的只有十几个块。
- **修法**：`chartTree()` 只导出画得出的层数与块数（3 层、每层最多 256 个最大子项）。导出 543→3.7 ms、解析 868→4.6 ms，图表节点数 30 万→3085。
- **QSG 不做**：布局/绘制在 1 层和 2 层都 <1 ms（2 层的细碎方块被面积裁剪掉了），QSGGeometryNode 优化不到瓶颈，而且软件/无头渲染下还要维护第二套回退。这一条按测量结果否掉，不是没做。
- **圆盘抗锯齿**：扇区路径从折线近似改成 `arcTo` 真圆弧，缩放时外缘不再有棱角。
- **剩余瓶颈**：`loadTree`（30 万节点约 0.5 s）在扫描完成和每次局部快照时执行；超大树的局部快照会卡这么久，是下一步要动的点。

### 阶段 12：把扫描结果的传递成本降下来

阶段 11 的测量把瓶颈指到了 `loadTree`：30 万节点约 0.5 s，而它每次局部快照都跑。

- **先做微基准**：单独遍历 30 万节点的 `QVariantMap` 树要 145 ms，加上路径拼接只多 6 ms。所以瓶颈不是路径，而是「每节点一次哈希查找 + 每个子项一次 `toMap()` 拷贝」。
- **改成扁平数组**：新增 `src/ScanTree.h` 的 `FlatTree`（并行数组：名称、父索引、大小、已分配、时间、属性、文件数、目录数、标志位）。`ScanService` 在**工作线程**上把 `diskview::Node` 直接摊成 `FlatTree` 再发给 GUI 线程（以前是嵌套 map，还多一次跨线程拷贝）；模型 `loadFlat()` 一趟填 `Entry`。父索引加「子节点总在父节点之后」保证不需要递归。
- **id 改成推导**：id 一直是 `位置+1`，不再存字符串，`indexForId` 直接解析数字——30 万个 QString 的分配没了。
- **结果**：30 万节点导入 517 → **136 ms**（扁平），嵌套路径顺带从 688 → 492 ms（也去掉了 id 字符串）。导出/解析仍是 3–4 ms。
- **快照仍会整树重建**：`loadFlat` 每次局部快照仍然全量重建模型。136 ms 在 2 秒的快照间隔下可以接受，但再大一个量级就该做增量更新了——这一步没做，记录在案。

### 阶段 13：接入 WinShun 宿主（B4 前半）

调研结论决定了做法：WinShun 的插件是**进程**（manifest 的 `runtime.program` + stdin/stdout 上的 JSON-RPC），不是导出函数的 DLL。同门的 `ra9fael.binarylab-qml` 用的是两段式：无 Qt 的启动器 + 跑在宿主 Qt 上的 DLL，为的是启动器能在严格沙箱里起来而不带任何运行时 DLL。这里照做。

- **启动器** `qmlapp/stub/main.cpp`：读 `WS_APP_DIR`、校验 `Qt6Core.dll`、`SetDefaultDllDirectories`+`AddDllDirectory`，`LoadLibraryExW` 加载同目录的 `DiskViewQmlApp.dll`，调用导出的 `winshunDiskViewQmlMain`。静态 CRT，实测只依赖 `KERNEL32.dll`。
- **DLL** `qmlapp/src/plugin_main.cpp`：`QGuiApplication` 在主线程、`Plugin::serve()` 在工作线程；`query` 返回磁盘行与 `disk.open`（与原生版同形），`action` 经 `BlockingQueuedConnection` 在 GUI 线程开窗，`shutdown` 关窗后退出。`initialized()` 把宿主的 `dataDir` 转成 `WS_DATA_DIR`，设置文件因此落在宿主的数据目录。
- **窗口复用**：`QmlHost` 原样使用，只加了 `window()`/`scanner()`/`settings()` 三个访问器和 `Options::importPath`。
- **清单** `qmlapp/plugin.json`：id `ra9fael.diskview.qml`（与原生版并存），程序 `diskview-qml.exe`。
- **打包** `tools/package-qml.py` → `dist/ra9fael.diskview.qml-0.1.0.wsplugin`（97 MB，1383 个文件）。
- **冒烟** `tools/verify/smoke-plugin-qml.py`：真实协议走完查询、打开、扫描、主题、关闭。

过程中踩到并记录的四个坑：

1. **`QGuiApplication` 构造前必须 `setLibraryPaths`**：不设的话它会阻塞地找 `platforms\qwindows.dll`，进程挂死、无任何输出。
2. **宿主的 Qt 没有 `QtQuick.Controls`**：`qml/QtQuick/Controls/` 只有样式子目录，没有顶层 `qmldir`，模块在 `Qt6QuickControls2.dll` 的资源里。手写 qmldir 补不上（缺 `qtquickcontrols2plugin.dll`）。
3. **因此随包带 Qt**：清单省略 `runtime.qt`/`qtVersion`（宿主表达「自带 Qt」的方式），包内附 SDK 的 Qt；`plugin_main` 若在同目录发现 `qml/QtQuick/Controls/qmldir` 就用自带的，否则回退宿主的。
4. **`QSaveFile` 在此部署下写不进去**（"Access is denied"，普通目录也一样），设置改成 `QFile` 直写，放弃原子写。

## 验证结果（阶段 13）
- `smoke-plugin-qml.py` 对构建目录与**解压后的安装目录**各跑一遍，都通过——说明包是自足的，不依赖宿主 Qt 或 PATH。
- 启动器 `dumpbin /dependents` 只有 `KERNEL32.dll`；DLL 依赖 `Qt6Core/Gui/Qml/Quick`。
- `build-plugin` 下 `diskview-settings-tests`、`diskview-model-tests` 全部 PASS。
- 删掉了所有临时探针，工作区干净。

## 验证结果（阶段 12）
- `--bench-chart 300000`：`model flat=136ms nested=492ms export=3.3ms`，布局/绘制仍 <1 ms。
- 模型单测新增扁平加载用例（顺序、路径拼接、计数、导航、文件视图）。
- `tests/check-prototype.ps1` 全绿：真实扫描走的就是 `loadFlat`，三张截图与全部自检无回归。

## 验证结果（阶段 11）
- `--bench-chart 50000 / 300000` 的数字见上表；两次运行稳定。
- `tests/check-prototype.ps1` 全绿；三种截图与自检（命中/变换/导航/列表/拖动/双击）无回归；圆盘截图人工确认圆弧边缘平滑、扇区与标签正确。

### 阶段 10：五处界面反馈

1. **筛选框下半截被裁**：视图行固定 34px，`TextField` 的隐式高度装不下。改为显式 26px、上下内边距 4、字号 11，行高 36。
2. **树没展开、列不够**：`onClicked` 现在对文件夹行做 `select + toggleExpanded`（双击仍进入）；列补到 WizTree 的 9 列。**修改时间/属性**在三条扫描路径上都补了：`Node` 增加 `modified`(FILETIME) 与 `attributes`(FILE_ATTRIBUTE_*)；`parallel_scan.h` 取 `WIN32_FIND_DATAW`，`scan.h` 的串行回退路径同样填，`mft_scan.h` 从 `$STANDARD_INFORMATION` 取并经 `scan_helper.h` 的 Row 协议（新增 `modified` 字段）带到宿主。属性按 Explorer 字母显示（R H S D A L C），时间格式 `yyyy-MM-dd HH:mm`；快照缺字段时显示「—」。列显隐放表头的「列」菜单，写进 `diskview-qml.json` 的 `hiddenColumns`。
3. **图例列**：按树表对齐——占比改成带条，新增「已分配」。
4. **文件视图语义**：保留清单式（当前子树的文件、按大小排），另加两个开关——范围（当前目录/整个扫描）与匹配（子串/模糊子序列）。模糊匹配只作用在文件名上，因为整树范围下匹配整条路径没有意义。
5. **设置对话框按钮出界**：`height: 520` 写死、内容长高后页脚被挤到框外。改成内容决定高度（`ColumnLayout` 不再 `anchors.fill`），排除列表给 150px 固定高度。
- 圆盘中心标签过长会溢出，按孔宽做 `ElideMiddle`。

### 阶段 8：图表交互与信息显示

手工在真实窗口里点了一遍，暴露的问题逐个修掉：

- **双击目录进不去**：两个原因。一是 `ChartItem` 只接管了按下，没有 `mouseReleaseEvent`，`m_dragging` 从不复位，点一下之后图表跟着鼠标漂移，双击的第二个落点已经不是原来那块；二是方块图默认递归到 24 层，父 tile 被子 tile 完全覆盖，能点到的永远是文件。补上 release、把平移挪进 `mouseMoveEvent`，并把「1 层/2 层」接到 `treemapTiles/sunburstSlices` 的 `maxDepth`（默认 1 层），目录才成为可双击的整块。另外把 `emit activated` 移出 `m_regions` 循环——QML 收到信号会重建这张表，在循环里 emit 等于让迭代器当场失效。
- **圆盘中心双击不返回**：判定半径写死 24px，而中心孔半径是 `maxR*0.22`（本窗口约 84px），点在 24~84px 的环里被当成普通扇区。改为按 `m_innerRadius` 判定。
- **方块/扇区没有信息**：照 `drawTiles` 与 `drawSlices` 的规则补微型标签（方块 ≥48×14 画名称、≥32 高补大小；扇区张角 >0.35 且环宽 >26px），文字颜色按底色亮度取黑/白。
- **悬停看不到信息**：补 `drawTooltip` 的等价物——QML 浮层显示名称/完整路径/「文件夹 · 大小 · 占比」/操作提示，数据来自 `ChartItem::hoverInfo(id)`（扫描时顺手建了 id→{名称,路径,大小,类型} 的索引，悬停不必再走树）。悬停只高亮，单击才选中，和原生的分工一致。
- **顶部控件重叠**：一行塞了路径框、状态和 5 个按钮，在 860px 最小宽度下挤到互相覆盖。拆成两行。
- **导航按钮过大 / 图标**：`Button`（隐式最小宽 64px）换成 28×28 的 `ToolButton`，用 Unicode 箭头 glyph 并带 tooltip；面包屑分隔符 15px、颜色加深，可点段加内边距扩大命中区。
- **右键菜单**：按「只补只读项」的决定，加了复制完整路径、在资源管理器中显示、定位到所在目录三项；文件操作留到宿主集成阶段。复制走 `QGuiApplication::clipboard`，资源管理器定位走 `ShellExecuteW`（含引号的路径直接拒绝，避免拼进参数）。
- 顺带把 QML 加载失败的诊断补上：引擎的 `warnings` 信号收集到错误文本里，`rightPadding` 那次误用就是靠它定位的。

### 阶段 7：列表面板与导航（B2）

对照原生 `viewer.h` 的列表面板逐条对齐，语义而不是外观照抄：

- **排序**：原生只有两个排序列（名称、大小）。点击表头 `s.ascending = s.sortName==name ? !s.ascending : name`（`viewer.h:1852`），即同一列翻转方向、换列取该列的自然方向（名称升序、大小降序）。QML 侧 `DiskViewModel::toggleSort` 与之逐字对应。名称比较用 `QString::compare(..., Qt::CaseInsensitive)`，对应原生的 `CompareStringOrdinal(...,TRUE)`。
- **过滤**：原生在 `EN_CHANGE` 时即过滤，比较的是整条路径的小写子串（`viewer.h:568-571`）。QML 侧 `setFilter` 用 `path.contains(filter, Qt::CaseInsensitive)`，`onTextEdited` 时写入，导航时清空（原生 `navigateTo` 也清空）。
- **最大文件**：原生在每个视图根上重算当前子树的 1000 个最大文件（`viewer.h:600-620`），根上是扫描期缓存的那份。QML 侧 `rebuildLargest()` 用同样大小上限 1000 的最小堆遍历当前子树，根节点的结果与扫描期缓存等价，因此没有单独维护扫描期榜单。
- **面包屑/历史**：原生是 `history` + `historyIndex`（`viewer.h:1666-1668`、`1690-1695`），`goUp` 也入栈。QML 侧 `m_history`/`m_historyPos` 同构，`pushHistory` 在目标等于当前位置时不重复入栈。面包屑由 `breadcrumb()` 回溯父链生成，QML 在超过 6 段时折叠成「首段 › … › 末四段」。
- **键盘**：按原生 `viewer_commands.h` 的列表键位接上（↑↓ 选择、→/回车进入、←/退格上一级、Alt+←/→ 历史、Ctrl+F、Ctrl+L、F5、Esc）。
- **翻页**：原生 `topScroll += ±maxRows`（`viewer.h:1854`）。QML 的 `ListView` 已虚拟化，底部按钮按整屏高度滚动，语义一致。
- **格式化**：`formatBytes` 的 1024 进制与「≥100 去掉小数」规则（`tree.h:54-73`）、`formatCount` 的三位分节（`tree.h:75-85`）照搬；`%` 相对当前视图根，`%.1f`。
- 顺带修掉一个启动期的信息缺失：QML 模块加载失败时 wrapper 只打一句「failed to load」，现在把引擎 `warnings` 信号里的具体行号一并带出（这条在排查 `rightPadding` 误用时立刻生效）。

## 验证结果（B1）- `tests/check-prototype.ps1` 全绿：`qml-treemap-native.png`（46967 字节）、`qml-sunburst-painted.png`（95237 字节）、`qml-scan-real.png`（45083 字节，自检里带 `pause=verified`）、`qml-scan-paused.png`（状态行 `scan	state=paused	mode=ordinary	workers=0	excluded=0	partials=0	files=0	dirs=0	skipped=0`）、helper 分派（`--scan-helper 'Z:' 坏管道名` 退出码 2，且不落进 GUI 的用法输出）。
- 新增 `tests/scan-tests.cpp`（`diskview-scan-tests.exe`，`scan tests: PASS`），三个用例：
  - 按住的扫描一个字节都不读（引擎级，400 ms 后 `seen==0`），放开后文件 3 个、目录 3 个；
  - `ScanService::startPaused()` → `paused()` 为真且模型 0 项 → `resume()` 后 `state=="done"`、文件 3、目录 3；
  - 用 `CreateFileW` 独占句柄锁住一个目录，扫描读不到它 → `skippedDirs()==1`、`issues().size()==1`，路径指向被锁目录、错误文本非空。
- 检查脚本的扫描一律读临时设置 `{"scanMode": 2}`、扫临时目录，因此自动化过程中不可能弹 UAC；快扫的 UAC 正常路径只能由人在盘符根目录上手动触发。
- 界面目视核对（截图 `build/qml-scan-paused.png`、`build/qml-settings-mode.png`）：顶栏「扫描已暂停」+「继续」按钮，底栏「扫描已暂停 · 点「继续」让它接着扫」，设置对话框新增「扫描方式 = 普通遍历」一行与勾选的「确定后立即重新扫描」。

## 验证结果（阶段 10）
- `tests/check-prototype.ps1` 全绿（自检三项 + 真实扫描 + held + helper）。
- 模型单测新增：文件视图整树范围（`wholeTree`）、模糊匹配（子序列命中/普通子串仍命中）、修改时间与属性的格式化与缺省「—」。
- 设置单测新增：`hiddenColumns` 只保留已知列、`fuzzyMatch`/`wholeTree` 往返。
- 原生目标单独用 `-DDISKVIEW_BUILD_QML=OFF` 编译通过：共享头文件（`tree.h`/`scan.h`/`parallel_scan.h`/`mft_scan.h`/`scan_helper.h`）的改动不破坏 `diskview.exe` 与 `diskview_tests.exe`。
- 截图核对：真实扫描的树表出现「修改时间 2026-11-13 02:18 / 属性 D、A」，图例带占比条与已分配列；设置对话框的「取消/确定」回到白框内。

## 验证结果（阶段 9）
- `tests/check-prototype.ps1` 全绿，自检的 `list=` 字段改成 `sort,name,filter,files,expand,history`：树表排序、过滤（逐行断言路径包含关键字）、文件视图只含文件、展开/折叠行数往返、历史后退前进。
- `diskview-model-tests` 扩到 40 项断言：新增文件视图、展开折叠（含子项层级深度）、扩展名聚合（按大小降序、文件计数、颜色与类型名）。过滤的老断言按新语义改写——命中行带上它上方的路径，筛选不改变用户的展开状态。
- 截图人工核对：固定快照下 5 行树表（% 父级条、已分配、项目/文件/文件夹）与 13 行扩展名图例（颜色与方块图一致）；真实扫描下 `alpha 20.0 KB / 已分配 28.0 KB`，图例只有 .bin 与 .txt 两种，方块图的 .txt 块是图例里的绿色。

## 验证结果（阶段 8）
- `tests/check-prototype.ps1` 全绿，自检报告新增 `chart` 一列：treemap `chart=double`、sunburst `chart=double,center`、真实扫描 `chart=double`。
- 图表交互改用真实鼠标事件验证：`drag` 检查发 press→move→release→hover，断言「拖动会平移、松开后悬停不再平移」；`chart` 检查发 press→release→dblclick→release，断言方块图双击目录能进入、圆盘在中心孔双击能回到上级。
- 两张截图人工核对：方块图 5 个顶层 tile 各自带名称与大小、悬停浮层内容正确；圆盘三个大扇区带标签、中心显示当前目录名；顶栏两行不再重叠，导航按钮为 28px 小图标。

## 验证结果（B2）
- `tests/check-prototype.ps1` 全绿，自检报告多出一列 `list=sort,name,filter,largest,history`：`0 8 regions transform navigate return list=... PASS`、`1 2 ... PASS`、真实扫描 `0 4 ... PASS`。
- 新增 `tests/model-tests.cpp`（`diskview-model-tests.exe`，`PASS`），32 项断言：默认大小降序；名称列升序/再点降序；切到大小列回到降序；过滤命中/忽略大小写/无命中/清空/匹配整条路径；最大文件在根上是 4 个文件、进入 `zulu` 后只剩它自己的 2 个；历史 back/forward/up 往返且 `goUp` 记得刚离开的目录；格式化 `512 B`、`1.5 KB`、`200 MB`、`1,234`。
- wrapper 自检新增 `runListChecks()`：排序（大小降序、名称升序）、过滤（用首行名称）、最大文件（只含文件）、历史（进入→后退→前进→后退）都由自检直接驱动模型完成，不依赖人眼。
- 截图核对 `build/qml-treemap-native.png` / `build/qml-scan-real.png`：右侧面板出现「当前层 / 最大文件」标签、过滤框、「名称 / 大小 ↓ / %」表头、底部「上一页 / 下一页」，真实扫描截图的百分比 90.9%/9.1% 与 fixture 的 57.3% 等都对得上。
- 交互类行为（鼠标点表头、键入过滤、真实按键）在无头环境无法合成；自检调用的是 UI 绑定的同一批模型方法，UI 事件本身仍需人手动确认。

## 阻塞及下一步

- 尚未接入 WinShun 协议、文件操作或 host plugin launcher；管理员快扫 helper 已接线，但 UAC 正常路径尚未由人手动跑过（需在固定 NTFS 卷的盘符根目录上触发）。
- QML 分支与原生版的双目标安装产物、plugin.json 变体发布尚未开始（用户已决定两个插件并存，`plugin.json` 出变体）。
- QPainter sunburst adapter 已完成功能/可见性检查；最终 Scene Graph 性能、大规模目录压力、实际 QML 鼠标双击合成事件测试仍未验收。原生 0.3.0 保持正式版及回退基线。
- B1–B4 已完成：交互、布局、性能、宿主接入与独立打包。剩下的是文件操作（回收站/删除）与发版流程（B5），以及文件视图整树范围的压力验证。
