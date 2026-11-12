# Disk View QML 技术样机

本目录是 `ra9fael.diskview` 的 **QML 分支（`qml`）** 里的 Qt Quick 应用目录，与原生版共存于同一棵仓库树：原生 `main.cpp`、`viewer.h`、`scan.h`、`tree.h` 仍在分支根部，QML 代码集中在 `qmlapp/`。当前阶段先验证 Qt Quick 的数据模型与双图表渲染；没有完整扫描器接入前，样机使用可重复的 JSON 快照。

## 当前状态

- 阶段 0：隔离目录与构建决策记录已建立。
- 阶段 1：独立启动的 wrapper（`diskview-qml.exe`）已完成，可脱离 WinShun 宿主单独运行、自检与截图。
- 阶段 2：真实扫描已接入（复用分支根部的 `scan.h`），固定快照仍作为默认数据源。
- 阶段 3：虚拟化列表待实施。
- 阶段 4：Scene Graph 方块图与圆盘图待实施（当前用 `QQuickPaintedItem` 绘制）。

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
- `qmlapp/tests/check-prototype.ps1` 覆盖 fixture 递归求和、chart geometry 单元测试、部署独立运行包、treemap/sunburst 双模式截图、可见节点命中与 zoom/pan 逆变换、模型目录进入/返回。

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
| `--self-test` | 跑命中/变换/导航自检并打印制表符分隔的报告 |
| `--help` | 用法 |

自检报告形如 `0<TAB>8<TAB>regions<TAB>transform<TAB>navigate<TAB>return<TAB>PASS`，末列 `PASS` 表示全部通过。

## 真实扫描

`ScanService` 复用分支根部的 `scan.h`（连同 `tree.h` 与 SDK 头 `winshun.h`），在工作线程上跑 `diskview::scanPath`，把结果转成与快照同形的树交给 `DiskViewModel::loadTree`。窗口顶栏有路径框、扫描与取消按钮，底栏显示文件数、目录数与耗时。

- 扫描不跟随符号链接/junction，不打开文件内容，云端占位文件不会被下载。
- 扫描与界面解耦：进度、取消和最终结果都通过队列回到 GUI 线程。
- 命令行 `--scan <路径>` 走的是同一条路径，`--self-test` 会额外验证扫描结果能进模型、能命中、能进目录再退回。

## 两个环境坑

- **必须用动态 CRT**：Qt 官方 DLL 是 `/MD` 编译的，静态 CRT（`/MT`）的 exe 与 Qt DLL 不共享堆，QML 类型注册跨堆写入时会触发堆断言。CMake 里固定为 `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`。
- **offscreen 平台没有字体**：offscreen 用的是 FreeType 字体库，不设 `QT_QPA_FONTDIR` 时所有文字都是空方框。wrapper 在 `QT_QPA_PLATFORM=offscreen`/`minimal` 时自动把 `QT_QPA_FONTDIR` 指向 `C:\Windows\Fonts`。

详见 [迁移执行记录](design/migration-status.md)。
