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

## 安全边界

- 不修改分支根部的 `plugin.json` 或原生 `diskview.exe`。
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
| `--help` | 用法 |

wrapper 的可执行文件同时是管理员快扫 helper：`--scan-helper <盘符> <管道名> <调用方 pid>` 会被 scan session 以 `runas` 提权启动，这条命令在 GUI 之前接管，退出码 2..7 表示不同的失败原因。

自检报告形如 `0<TAB>8<TAB>regions<TAB>transform<TAB>navigate<TAB>return<TAB>list=sort,name,filter,largest,history<TAB>PASS`，末列 `PASS` 表示全部通过。带 `--scan` 时还会多一行 `scan<TAB>state=…<TAB>mode=…<TAB>workers=…<TAB>excluded=…<TAB>partials=…<TAB>files=…<TAB>dirs=…<TAB>skipped=…<TAB>pause=…`。`pause=verified` 表示这次扫描确实被按住过（按住期间模型读到 0 项），`mode=fast` 表示走的是 MFT 快扫。

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

## 列表面板与导航

右侧列表与原生版同语义：三列（名称 / 大小 / %），两个模式标签「当前层」「最大文件」。

- **排序**：点「名称」按名称排（大小写不敏感），点「大小」按大小排；点同一个表头再点一次翻转方向，换表头则按各自的自然方向（名称升序、大小降序）。默认最大在前。
- **过滤**：输入即过滤，匹配的是整条路径（大小写不敏感），图表不受影响。进入/返回目录时过滤自动清空，和原生版一致。
- **最大文件**：当前子树里最大的 1000 个文件，用最小堆维护；在根目录时就是整棵树。进入子目录后排名跟着变成该子树。
- **导航**：可点击的面包屑（超过 6 段时折叠成「首段 › … › 末四段」）、前进/后退/上一级三个按钮，历史栈在同一处维护；双击行或按回车进入目录。
- **键盘**：`↑`/`↓` 移动选择，`→`/回车进入目录，`←`/退格返回上一级，`Alt+←`/`Alt+→` 前进后退，`Ctrl+F` 聚焦过滤框，`Ctrl+L` 聚焦路径框，`F5` 重扫，`Esc` 清空过滤。
- **翻页**：列表用 `ListView` 虚拟化渲染，底部「上一页」「下一页」按整屏滚动，和原生版的 Prev/Next 一致。
- **格式化**：大小按 1024 进制，数值 ≥100 时省掉小数（`200 MB`、`1.5 KB`、`512 B`）；条目数按三位加逗号（`1,234`）；`%` 相对当前视图根，保留一位小数。

## 局部快照

扫描过程中，扫描器每隔一段时间把当前已经读到的树发回来一次，界面随即刷新，不必等整个目录读完。底栏的来源标签会区分「扫描中」「已取消的扫描」「扫描失败」和「固定快照」；被取消的扫描仍会显示它已经读到的那部分，并标注「部分结果 · 扫描未完成，数据仍在变化」。

## 三个环境坑

- **必须用动态 CRT**：Qt 官方 DLL 是 `/MD` 编译的，静态 CRT（`/MT`）的 exe 与 Qt DLL 不共享堆，QML 类型注册跨堆写入时会触发堆断言。CMake 里固定为 `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`。
- **offscreen 平台没有字体**：offscreen 用的是 FreeType 字体库，不设 `QT_QPA_FONTDIR` 时所有文字都是空方框。wrapper 在 `QT_QPA_PLATFORM=offscreen`/`minimal` 时自动把 `QT_QPA_FONTDIR` 指向 `C:\Windows\Fonts`。
- **offscreen 的通用字族名不可信**：那套 FreeType 字体库把通用的 "Sans Serif" 解析成了 Agency Gothic CT，拉丁字母的字形是错的（"QML" 显示成 "D4"）。wrapper 在 offscreen/minimal 下把应用字体显式定为 Segoe UI，并留 Microsoft YaHei 等中文字体兜底。原生（windows/D3D11）路径不受影响。

详见 [迁移执行记录](design/migration-status.md)。
