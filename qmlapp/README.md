# Disk View QML 技术样机

本目录是 `ra9fael.diskview` 的 **QML 分支（`qml`）** 里的 Qt Quick 应用目录，与原生版共存于同一棵仓库树：原生 `main.cpp`、`viewer.h`、`scan.h`、`tree.h` 仍在分支根部，QML 代码集中在 `qmlapp/`。当前阶段先验证 Qt Quick 的数据模型与双图表渲染；没有完整扫描器接入前，样机使用可重复的 JSON 快照。

## 当前状态

- 阶段 0：隔离目录与构建决策记录已建立。
- 阶段 1：已定位 Qt 6.12.0 MSVC SDK；最小 host Qt 启动器验证进行中。
- 阶段 2：固定 JSON 快照桥接和真实扫描器接入待实施。
- 阶段 3：虚拟化列表待实施。
- 阶段 4：Scene Graph 方块图与圆盘图待实施。

## 安全边界

- 不修改分支根部的 `plugin.json` 或原生 `diskview.exe`。
- 不进行真实文件操作；快照数据只用于可视化。
- 不启动新的插件或服务端。

## 构建与验证

构建必须在加载了 MSVC 环境的终端里进行，否则 CMake 的编译器探测会失败：

```
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S qmlapp -B qmlapp/build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build qmlapp/build-cmake
```

- `qmlapp/CMakeLists.txt` 是独立的原型工程，尚未挂进分支根部的 `CMakeLists.txt`。
- `qmlapp/tests/check-prototype.ps1` 覆盖 fixture 递归求和、chart geometry 单元测试、treemap/sunburst 双模式截图、可见节点命中与 zoom/pan 逆变换、模型目录进入/返回。

详见 [迁移执行记录](design/migration-status.md)。
