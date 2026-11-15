import QtQuick

// The 6 px strip at a column's right edge. Dragging changes that column's
// width in the panel's own ColumnStore; double-click restores the spec's
// default width for the column.
//
// The delta is measured in the window's coordinate space, not in the handle's
// own: the handle is anchored to the edge it moves, so a local delta would be
// cancelled by the handle moving with it (the drag used to stick and jitter).
Item {
    id: handle

    required property var store
    required property string column
    property int minimum: 40
    property int maximum: 480

    implicitWidth: 7
    implicitHeight: 20

    Rectangle {
        anchors.centerIn: parent
        width: mouse.containsMouse || mouse.pressed ? 2 : 1
        height: parent.height - 6
        color: mouse.containsMouse || mouse.pressed ? theme.accent : theme.border
    }

    MouseArea {
        id: mouse
        objectName: "columnHandleMouse"
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real startSceneX: 0
        property real startWidth: 0
        onPressed: function(point) {
            startSceneX = mapToItem(null, point.x, point.y).x
            startWidth = handle.store.width(handle.column)
        }
        onPositionChanged: function(point) {
            if (!pressed) return
            const sceneX = mapToItem(null, point.x, point.y).x
            const wanted = Math.round(startWidth + sceneX - startSceneX)
            handle.store.setWidth(handle.column, Math.max(handle.minimum, Math.min(handle.maximum, wanted)))
        }
        onDoubleClicked: handle.store.reset(handle.column)
    }
}
