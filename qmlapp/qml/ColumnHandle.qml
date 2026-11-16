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
    property var resizeBy: function(delta) { store.setWidth(column, Math.max(0, effectiveWidth() + delta)) }
    property var effectiveWidth: function() { return store.width(column) }
    property var autoFit: function() { store.reset(column) }

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
        property bool editing: false
        onPressed: function(point) {
            handle.store.beginEdit()
            editing = true
            startSceneX = mapToItem(null, point.x, point.y).x
        }
        onPositionChanged: function(point) {
            if (!pressed) return
            const sceneX = mapToItem(null, point.x, point.y).x
            handle.resizeBy(Math.round(sceneX - startSceneX))
            startSceneX = sceneX
        }
        onReleased: if (editing) { editing = false; handle.store.endEdit() }
        onCanceled: if (editing) { editing = false; handle.store.endEdit() }
        onDoubleClicked: handle.autoFit()
    }
}
