# 0.1.15 接入 WinShun 外观契约

参考 `ra9fael.tio` 的 `qmlapp/ThemeBridge.*` 与 `DesignTokens.qml`。

## 数据来源

宿主在 `initialize` 与 `appearance` 通知里下发：

- `uiStyle`：fluent / brand / tactile / liquid
- `uiSize`：compact / normal(standard) / roomy
- `corners`：round / square
- `transparency`：on / off
- `nativeLook`：`style` `dark` `rounded`，整套颜色（`#AARRGGBB`）与圆角

颜色字段按 tio 的做法解析；缺失字段用 fluent 近似表兜底，因此旧宿主也能用。

## 实现

- `ThemeBridge::setAppearance(QVariantMap)`：接受 initialize 的嵌套
  `nativeLook`，也接受通知形式（字段在顶层）；
- 映射：
  - `window = over(background, page)`
  - `surface = over(layer, window)`
  - `surfaceAlt = over(control, window)`
  - `border = over(divider, surface)`
  - `hoverRow = over(hover, surface)`
  - `selection = over(tagFill, surface)`（缺失时用 pressed）
  - `accent`/`accentText`/`error(danger)`
  - `radius = rounded ? controlRadius : 0`
  - 控件高度/字号由 `uiSize` 决定
- 半透明 token 先合成到不透明底（透明效果打开时）。
- 无 appearance（独立运行）时保留原浅/深表，行为不变。
- `plugin_main.cpp`：
  - `initialized()` / `appearanceChanged()` 保存 `appearance().dump()` 与主题字符串；
  - 窗口创建后应用，后续通知在 GUI 线程重设；
  - 宿主设置同步仍保持关闭（与外观无关）。
- `Main.qml`：把 token 喂给根窗口的 `palette`，Basic 风格的 TextField/
  Button/ComboBox 不再在深色下保持白底。

## 验证

- `WS_APPEARANCE` 注入 brand 深色：窗口、面板、路径框、按钮、下拉框均为深色。
- 注入 liquid 浅色：还原浅色，控件同步。
- 无 appearance 的独立运行：自检通过，配色不变。
- 未在真实宿主里逐一切换四种风格；安装后可直接在 WinShun 设置里切换查看。
