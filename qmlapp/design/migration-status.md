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

### 阶段 2：快照与扫描接口

优先把 `tree.h` 保持为 C++ 核心；需要先拆分或适配 `scan.h` 对 `winshun.h` 的依赖，且不能让 QML 持有裸 `Node*` 跨快照。

### 阶段 3：列表模型

已完成固定 JSON 快照的 `DiskViewModel`：C++ 持有节点、稳定 ID、父子关系、大小格式化和当前目录；QML 使用 `QAbstractListModel` + `ListView`，截图已验证当前层显示 5 项。真实 `ScanResult` 尚未接入，当前仍是固定快照。

### 阶段 4：图表样机

已把正式 `tree.h` 的 `treemapTiles` 与 `sunburstSlices` 接入 ChartItem；treemap/sunburst 共享缓存几何和命中坐标，支持滚轮缩放、拖动平移、悬停选择、双击目录进入。圆盘中心显示当前目录名，双击中心返回父级。绘制使用 `QQuickPaintedItem`/QPainterPath；先前 QSGGeometryNode 在当前 software/offscreen 环境没有呈现，故暂用可靠适配器，性能路径尚未验收。

## 本轮验证结果

- Qt SDK：`D:/tools/Qt/6.12.0/msvc2022_64`；host DLL：Qt 6.12.0.0；MSVC 19.51.36260。
- CMake 配置与构建通过（`vcvars64.bat` + Ninja，MSVC 19.51.36260），产物 `build-cmake/bin/diskview-qml-prototype.exe`；早前的手工 `moc` + `cl` + `link` 链路已由 CMake 取代。
- `tests/check-prototype.ps1` 通过：递归 fixture 总和校验、C++ chart geometry tests、两种模式截图、可见节点 hit-test、zoom/pan inverse transform、DiskViewModel 目录进入/返回。treemap 截图 `build/qml-treemap-native.png` 约 47 KB（mode 0 命中 ID `8`）；sunburst 截图 `build/qml-sunburst-painted.png` 约 95 KB（mode 1 命中 ID `2`）。
- 发现 fixture 的 `WinShun/build/debug` 子项总和比声明多 1 MiB，已修复样机 JSON 并加入 `tests/check-fixture.cjs` 递归检查。截图中的未填充扇区代表叶文件没有下一层子项，并非绘制断裂。
- HTML 交互演示位于 `../ra9fael.diskview/design/compact-layout-prototype.html`，提示文案已区分浏览器仿真与 Qt 实测，并提供 Qt 截图及 QML 源码链接。`node build/compact-prototype-check.cjs`：26 项全部通过。

## 阻塞及下一步

- 按用户要求先测功能，当前仍使用固定 JSON fixture；尚未接入 WinShun 协议、真实扫描、MFT helper、文件操作或 host plugin launcher。
- `qmlapp/CMakeLists.txt` 仍是独立工程，尚未挂进分支根部的 CMake；QML 分支与原生版的双目标构建、plugin.json 变体发布尚未开始。
- QPainter sunburst adapter 已完成功能/可见性检查；最终 Scene Graph 性能、大规模目录压力、实际 QML 鼠标双击合成事件测试仍未验收。原生 0.3.0 保持正式版及回退基线。
