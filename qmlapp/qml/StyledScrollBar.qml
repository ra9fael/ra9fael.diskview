import QtQuick
import QtQuick.Controls

// An overlay scroll bar: shown while the view scrolls or the pointer is on
// the bar, faded out a moment after it goes idle.
ScrollBar {
    id: control

    implicitWidth: 10
    implicitHeight: 10
    minimumSize: 0.05

    background: Rectangle { color: "transparent" }
    contentItem: Rectangle {
        id: thumb
        implicitWidth: 10
        implicitHeight: 10
        radius: 5
        color: control.pressed ? theme.text : control.hovered ? theme.textMuted : theme.scrollThumb
        opacity: 0

        states: State {
            name: "shown"
            when: control.policy === ScrollBar.AlwaysOn || control.hovered || (control.active && control.size < 1.0)
            PropertyChanges { thumb.opacity: 1 }
        }
        transitions: Transition {
            from: "shown"
            SequentialAnimation {
                PauseAnimation { duration: 1200 }
                NumberAnimation { target: thumb; property: "opacity"; to: 0; duration: 300 }
            }
        }
    }
}
