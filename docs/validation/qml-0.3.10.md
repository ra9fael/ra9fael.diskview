# 0.3.10 Dock 预设、主题与文件操作界面

## 变更

- 预设布局菜单提供列表在上、下、左、右四种排列；允许继续拖拽、合并和最大化，自定义布局持久化。
- Dock 分隔线具有可见边框与悬停状态，扩大拖动区域。
- 文件工具栏统一控件高度、预留列菜单空间，下拉面板保证最小内容宽度。
- 树与文件列表共用 `PercentCell.qml`，百分比配色一致。
- 图表绑定窗口主题 token，深浅主题可实时更新。
- 删除弹窗按钮位于框内，永久删除使用确认/取消，错误与操作结果可见。
- Shell 文件操作使用原生路径分隔符；命令行诊断与界面共用删除实现，COM 对象在反初始化前释放。

## 验证

- 独立程序构建成功。
- `diskview_dock`、`diskview_chart_geometry`、`diskview_scan_settings`、`diskview_scan_behaviour`、`diskview_list_model`：5/5 通过。
- `--self-test --fixture-only --delay 1500` 返回 0 并输出 `PASS`：预设菜单、实时主题、Dock 拖拽、列表导航和列菜单通过。
- 树/文件列宽实际拖动分别为 `72→96`、`84→108`；相邻列同步缩小，总宽 `1254` 不变；双击自动适配为 `57`，与计算值一致；扩展名面板 `legend=shown`。
- 截图确认预设菜单、永久删除弹窗、模拟回收错误、深色图表和左列表预设。模拟错误截图只验证显示，不证明真实回收成功。
- D: 新建测试文件执行 `--file-action delete` 返回 0，文件消失。
- 插件目标构建成功；本地包 `dist/ra9fael.diskview.qml-0.3.10.wsplugin`，8 个文件、3070 KB，SHA256 `49735acaadadb9feeb1ec92e3266c9f7c3353bf518fe7e2495a8e66c75848e64`。
- `ws plugin install` 从 0.3.9 升级成功，`ws plugin list --json` 确认 `0.3.10 / on / ready`。仅本机安装。
- 截图保存在 [qml-0.3.10](qml-0.3.10/)。

## 已知问题

- 回收站尚未通过端到端验证：Shell 接受操作标志，但 `PreDeleteItem` 收到 `0x202`，不含 `TSF_DELETE_RECYCLE_IF_POSSIBLE`。保护逻辑中止，原文件保留。原因未确定。
- C: 先前文件操作返回 `0x80270021`，Windows SDK 定义为 `COPYENGINE_E_ACCESS_DENIED_SRC`（源访问被拒绝）。
- `diskview_logic` 与 `diskview_protocol` 在先前干净 HEAD 验证中同样失败；本轮针对性测试不包含这两项。
- 截图使用 offscreen 软件渲染，未验证真实宿主 D3D11/Mica 观感。
