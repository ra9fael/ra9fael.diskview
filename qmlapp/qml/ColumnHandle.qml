import QtQuick

// The 6 px strip between two column headers. Dragging changes the width on the
// panel's own width object; double-click restores the default. `widths` is one
// of the QtObjects in Main.qml (tree, file or legend columns), so the three
// panels stay independent and can be persisted separately.
Item {
    id: handle

    required property var widths
    required property string column
    property int defaultWidth: 0
    property int minimum: 40

    implicitWidth: 6
    implicitHeight: 20
    LayoutMirroring.enabled: false

    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: parent.height - 6
        color: mouse.containsMouse || mouse.pressed ? theme.accent : "transparent"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real start: 0
        onPressed: start = mouse.x
        onPositionChanged: {
            if (!pressed) return
            const delta = mouse.x - start
            if (delta === 0) return
            widths[column] = Math.max(minimum, widths[column] + delta)
            start = mouse.x
        }
        onDoubleClicked: if (defaultWidth > 0) widths[column] = defaultWidth
    }
}
