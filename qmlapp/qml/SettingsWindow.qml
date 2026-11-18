import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// The plugin's own settings window: the host's card button and the launcher's
// settings entry open it. It holds the settings and nothing else, so looking at
// them builds no scanner and reads no disk tree.
ApplicationWindow {
    id: window

    width: 520
    height: 660
    minimumWidth: 460
    minimumHeight: 520
    visible: true
    title: "磁盘透视设置"
    font.family: metrics.fontFamily
    color: theme.backdrop ? theme.surface : theme.window

    // The Basic style's controls take their colours from the palette; feeding
    // it the same tokens keeps a TextField, Button and ComboBox from staying
    // light in a dark WinShun.
    palette.window: theme.backdrop ? theme.surface : theme.window
    palette.windowText: theme.text
    palette.base: theme.control
    palette.alternateBase: theme.surfaceAlt
    palette.text: theme.text
    palette.button: theme.control
    palette.buttonText: theme.text
    palette.brightText: theme.error
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    palette.placeholderText: theme.textMuted
    palette.toolTipBase: theme.backdrop ? theme.surfaceAlt : theme.surface
    palette.toolTipText: theme.text
    palette.mid: theme.border
    palette.midlight: theme.border
    palette.dark: theme.border
    palette.light: theme.surface
    palette.shadow: theme.border

    SettingsContent {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        // Applying keeps the window open: the status line says what happened,
        // and the next scan picks the values up.
        onCancelled: window.close()
    }

    Component.onCompleted: content.begin()
}
