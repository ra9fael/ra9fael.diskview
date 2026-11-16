# 0.3.11 选中同步与异步回收验证

## 变更

- 树选中行使用主题选中背景和左侧强调线。
- 可视化单击展开树中的祖先并选中对应行；树单击继续通过共享选中 ID 更新可视化。双击进入目录后保留树选中高亮。
- 选中定位在列表更新后执行。
- `--file-action` 诊断通过实际确认按钮的鼠标事件触发异步操作，并等待完成信号。

## 验证

- 独立程序及插件目标构建成功。
- offscreen 软件渲染和 Windows D3D11 窗口自检均返回 0，输出 `selection-sync`、`theme-live`、`PASS`。选中同步检查读取实际树 delegate 的高亮和背景，验证树单击、图表单击及图表目录激活。
- Windows 窗口截图：[选中行](qml-0.3.11/selection.png)。
- 同一原生探针在 D: 项目路径运行时收到 `0x202`，复制至 C: 临时目录运行时收到 `0x282`；两者均非 elevated、非 restricted，保护回调保持开启。
- 实际程序从 C: 临时目录执行 `--file-action recycle`：确认按钮触发异步回收，返回 0，源文件消失。offscreen 与 Windows D3D11 均通过。
- D: 回收站元数据精确匹配 `build/recycle-fix/async.txt` 和 `build/recycle-fix/desktop.txt` 的完整原路径，对应内容分别为 `async dialog fixture`、`desktop async fixture`。
- 本机插件从 0.3.10 升级至 0.3.11，`ws plugin list --json` 显示 `on / ready`，安装位置在 C: 用户插件目录。

## 已知限制

- D: 开发目录的进程仍无法回收；路径相关的 Windows 行为根因未确定。失败时保护逻辑保留源文件。
- 回收成功证据来自独立程序的真实 QML 弹窗与异步链路，尚未验证宿主加载 DLL 后的实际回收交互。
