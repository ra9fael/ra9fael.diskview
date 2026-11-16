import QtQuick
import QtQuick.Controls

ScrollBar {
    id: control

    implicitWidth: 10
    implicitHeight: 10

    background: Rectangle { color: "transparent" }
    contentItem: Rectangle {
        implicitWidth: 10
        implicitHeight: 10
        radius: 5
        color: control.pressed ? theme.text : control.hovered ? theme.textMuted : theme.scrollThumb
        opacity: control.active ? 1 : 0.6
    }
}
