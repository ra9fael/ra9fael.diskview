# Disk View QML 0.1.4

## 使用

- 在图表工具栏的布局菜单选择 **自由 Dock**。拖动标题到面板上、下、左、右拆分，拖到中央合并为 Tab；Tab 可拖出重新停靠。
- 拖动分割线调整比例；松开保存比例，重启恢复 `dockLayout`。工具栏布局图标恢复默认 Dock。
- 面板标题双击或右端按钮独立全屏，Esc / 还原按钮回到停靠位置。全屏使用独立浮层。
- 树、文件列表、扩展名、图表共享选择。点击扩展名过滤文件列表并弱化图表其他扩展名；再次点击清除。
- 图表复位回扫描根目录并复位缩放/平移。树大纲保持原根，图表双击目录下钻。
- 快扫在插件进程内读取 MFT，沿用宿主提供的管理员令牌，不另启 runas/UAC。普通权限或不满足固定 NTFS 根目录条件时回退普通遍历。

## 本轮边界

按用户 2026-11-15 的范围调整，暂不同步宿主外观和扫描设置；使用独立默认配色及本地 `diskview-qml.json`。上游桥接合并后再对接四种宿主风格。本轮不替换或重启 WinShun。

Dock 是窗口内停靠，不提供独立桌面浮窗。管理员 WinShun 内实际快扫尚未重新验证，不能以普通权限自动测试替代。

## 验证入口

```powershell
# Qt bin 需位于 PATH；测试使用隔离设置，不修改用户配置。
qmlapp/tests/check-dock.ps1
powershell -NoProfile -File qmlapp/tests/check-prototype.ps1 -SkipBuild
```

Dock 检查覆盖方块/圆盘 × 100/125/150/175% 缩放、窗口鼠标事件 Tab 合并/拖出、分割线、全屏还原以及保存后重启。布局单测验证四面板唯一性、JSON 往返、非法布局拒绝。

## English

0.1.4 adds an in-window drag-and-tab Dock workspace, persistent splitter ratios,
independent pane fullscreen, a shared file/extension selection, chart reset and
default outlines. NTFS fast scans run in the plugin process using the elevation
provided by WinShun; the plugin does not launch a second UAC helper.

Host appearance and scan-setting synchronisation is intentionally disabled until
the upstream bridge is merged. Local defaults remain authoritative. No running
WinShun instance is replaced or restarted for this delivery.
