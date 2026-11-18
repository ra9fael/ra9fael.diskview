import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// A push button drawn from the same tokens as the rest of the window, the
// way WinShun's own FlatButton is: the control fill and its border, a hover
// and a pressed step, the host's control radius, and an optional Phosphor
// glyph before the text. The Basic style's default button reads flat and
// translucent on Mica; this one stays a solid control.
Button {
    id: button

    property string iconName: ""
    property bool primary: false
    // A slim row (a pane-header bar, a dialog footer) can ask for a lower
    // button; anything else keeps the host's control height.
    property real compactHeight: -1
    implicitHeight: compactHeight > 0 ? compactHeight : metrics.control
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: theme.fontSize

    background: Rectangle {
        radius: theme.controlRadius
        color: !button.enabled ? theme.surfaceAlt
             : button.primary ? theme.accent
             : button.pressed ? theme.pressed
             : button.hovered ? theme.controlHover
             : theme.control
        border.width: button.primary ? 0 : 1
        border.color: !button.enabled ? theme.border
                    : button.primary ? theme.accent
                    : button.pressed ? theme.accent
                    : button.hovered ? theme.controlBorder
                    : theme.controlBorder
    }

    contentItem: Row {
        anchors.centerIn: parent
        spacing: 6

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.iconName.length > 0
            iconName: button.iconName
            size: 14
            color: !button.enabled ? theme.textMuted : button.primary ? theme.accentText : theme.text
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: !button.enabled ? theme.textMuted : button.primary ? theme.accentText : theme.text
            font.pixelSize: button.font.pixelSize
            verticalAlignment: Text.AlignVCenter
        }
    }
}
