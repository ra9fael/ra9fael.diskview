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
    implicitHeight: metrics.control
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: theme.fontSize

    background: Rectangle {
        radius: theme.controlRadius
        border.width: 1
        color: !button.enabled ? theme.surfaceAlt
             : button.pressed ? theme.pressed
             : button.hovered ? theme.controlHover
             : theme.control
        border.color: !button.enabled ? theme.border
                    : button.pressed ? theme.accent
                    : button.hovered ? theme.controlBorder
                    : theme.controlBorder
    }

    contentItem: Row {
        spacing: 6

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.iconName.length > 0
            iconName: button.iconName
            size: 14
            color: button.enabled ? theme.text : theme.textMuted
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: button.enabled ? theme.text : theme.textMuted
            font.pixelSize: button.font.pixelSize
            verticalAlignment: Text.AlignVCenter
        }
    }
}
