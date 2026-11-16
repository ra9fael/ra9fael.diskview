import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

pragma ComponentBehavior: Bound

// The existing panes are placed, not instantiated. Moving a tab therefore
// preserves chart transforms, model selection and list scroll positions.
//
// Every tab group gets a body item of its own; the active pane is anchored to
// that body. Anchoring (rather than assigning x/y/width/height by hand) also
// survives panes that are themselves layouts.
Item {
    id: workspace
    required property var panes
    required property var state
    // The tab strip's height, passed in from the window's metric store.
    required property int rowHeight
    property string fullscreen: ""
    property var geometry: []
    property string dragged: ""
    property string target: ""
    property string edge: ""
    property point pointer
    property var collapsedGroups: ({})
    // path -> body item, filled by the body repeater.
    property var bodies: ({})
    signal maximizeRequested(string pane)
    signal persisted()

    function groupCollapsed(id) { return collapsedGroups[id] === true }
    function toggleGroup(id) {
        const next = Object.assign({}, collapsedGroups)
        next[id] = !groupCollapsed(id)
        collapsedGroups = next
        refresh()
    }

    function title(id) {
        return id === "tree" ? "树视图" : id === "files" ? "文件视图"
             : id === "chart" ? "可视化视图" : "扩展名视图"
    }
    function icon(id) {
        return id === "tree" ? "tree-structure" : id === "files" ? "rows"
             : id === "chart" ? "chart-donut" : "tag"
    }

    function registerBody(path, body) {
        const next = Object.assign({}, bodies)
        next[path] = body
        bodies = next
        Qt.callLater(refresh)
    }
    function unregisterBody(path) {
        const next = Object.assign({}, bodies)
        delete next[path]
        bodies = next
    }

    function refresh() {
        if (!width || !height) return
        geometry = state.placements(width, height)
        for (const id of ["tree", "files", "legend", "chart"]) {
            const pane = panes[id]
            if (!pane || fullscreen === id) continue
            pane.visible = false
            pane.clip = true
        }
        for (const slot of geometry) {
            if (slot.type !== "tabs") continue
            const body = bodies[slot.path]
            if (!body) continue
            const collapsed = groupCollapsed(slot.active)
            for (const id of slot.panes || []) {
                const pane = panes[id]
                if (!pane || fullscreen === id) continue
                if (id === slot.active && !collapsed) {
                    if (pane.parent !== body) {
                        pane.anchors.fill = null
                        pane.parent = body
                    }
                    pane.anchors.fill = body
                    pane.visible = true
                }
            }
        }
    }

    function hoverDrop(position) {
        pointer = position
        target = ""
        edge = ""
        for (const slot of geometry) {
            if (slot.type !== "tabs") continue
            if (position.x < slot.x || position.x > slot.x + slot.width
                || position.y < slot.y || position.y > slot.y + slot.height) continue
            const panesInGroup = slot.panes || []
            target = slot.active === dragged
                ? (panesInGroup.find(candidate => candidate !== dragged) || dragged)
                : slot.active
            const x = (position.x - slot.x) / slot.width
            const y = (position.y - slot.y) / slot.height
            if (x > .3 && x < .7 && y > .3 && y < .7) edge = "center"
            else {
                const d = Math.min(x, 1 - x, y, 1 - y)
                edge = d === x ? "left" : d === 1 - x ? "right" : d === y ? "top" : "bottom"
            }
            return
        }
    }
    onWidthChanged: Qt.callLater(refresh)
    onHeightChanged: Qt.callLater(refresh)
    onVisibleChanged: if (visible) Qt.callLater(refresh)
    Connections {
        target: workspace.state
        function onChanged() { workspace.refresh(); workspace.persisted() }
    }

    // Each tab group's body: below its tab strip, filling the rest of the slot.
    Repeater {
        model: workspace.geometry
        delegate: Item {
            id: body
            required property var modelData
            objectName: "dock-body-" + modelData.path
            visible: modelData.type === "tabs"
            x: modelData.x
            y: modelData.y + rowHeight
            width: modelData.width
            height: Math.max(0, modelData.height - rowHeight)
            Component.onCompleted: workspace.registerBody(modelData.path, body)
            Component.onDestruction: workspace.unregisterBody(modelData.path)
        }
    }

    // The tab strips and the split handles.
    Repeater {
        model: workspace.geometry
        delegate: Item {
            id: region
            required property var modelData
            objectName: modelData.type === "handle" ? "dock-handle-" + modelData.path : "dock-group-" + modelData.active
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.type === "tabs" ? rowHeight : modelData.height
            z: 2
            // A tab group's own strip. The split handle gets a visible line in
            // the same border colour, and thickens to the accent while the
            // pointer is on it, so the divider reads as something draggable.
            Rectangle {
                anchors.fill: parent
                visible: region.modelData.type === "tabs"
                color: theme.surfaceAlt
                border.color: theme.border
            }
            Rectangle {
                id: divider
                visible: region.modelData.type === "handle"
                anchors.centerIn: parent
                width: region.modelData.horizontal
                       ? (handleArea.containsMouse || handleArea.pressed ? 3 : 1)
                       : region.width
                height: region.modelData.horizontal
                        ? region.height
                        : (handleArea.containsMouse || handleArea.pressed ? 3 : 1)
                color: handleArea.containsMouse || handleArea.pressed ? theme.accent : theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                spacing: 2
                visible: region.modelData.type === "tabs"
                Repeater {
                    model: region.modelData.panes || []
                    delegate: Rectangle {
                        id: tab
                        required property string modelData
                        objectName: "dock-tab-" + modelData
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: 160
                        color: region.modelData.active === modelData ? theme.selection
                                                                       : (mouseArea.containsMouse ? theme.hoverRow : "transparent")
                        Row {
                            anchors.centerIn: parent
                            spacing: 4
                            Glyph { iconName: workspace.icon(tab.modelData); size: 13; color: region.modelData.active === tab.modelData ? theme.accent : theme.textMuted }
                            Label { text: workspace.title(tab.modelData); font.family: Metrics.fontFamily; font.pixelSize: Metrics.caption; font.weight: region.modelData.active === tab.modelData ? Font.DemiBold : Font.Normal; color: region.modelData.active === tab.modelData ? theme.text : theme.textMuted }
                        }
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            cursorShape: Qt.OpenHandCursor
                            property point start
                            property bool moving: false
                            onPressed: function(event) {
                                start = mapToItem(workspace, event.x, event.y)
                                moving = false
                            }
                            onPositionChanged: function(event) {
                                if (!pressed) return
                                const at = mapToItem(workspace, event.x, event.y)
                                if (Math.abs(at.x - start.x) + Math.abs(at.y - start.y) > 8) moving = true
                                if (moving) {
                                    workspace.dragged = tab.modelData
                                    workspace.hoverDrop(at)
                                }
                            }
                            onReleased: {
                                const pane = tab.modelData
                                const target = workspace.target
                                const edge = workspace.edge
                                workspace.dragged = ""
                                workspace.target = ""
                                if (moving && target) workspace.state.movePane(pane, target, edge)
                                else if (!moving) workspace.state.activate(pane)
                            }
                            onCanceled: { workspace.dragged = ""; workspace.target = "" }
                            onDoubleClicked: workspace.maximizeRequested(tab.modelData)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    implicitWidth: rowHeight; implicitHeight: rowHeight
                    ToolTip.visible: hovered
                    ToolTip.text: workspace.groupCollapsed(region.modelData.active) ? "展开" : "折叠"
                    onClicked: workspace.toggleGroup(region.modelData.active)
                    contentItem: Glyph { iconName: workspace.groupCollapsed(region.modelData.active) ? "plus" : "minus"; size: 14; color: theme.textMuted }
                }
                ToolButton {
                    implicitWidth: rowHeight; implicitHeight: rowHeight
                    ToolTip.visible: hovered
                    ToolTip.text: workspace.fullscreen === region.modelData.active ? "还原" : "最大化"
                    onClicked: workspace.maximizeRequested(region.modelData.active)
                    contentItem: Glyph { iconName: workspace.fullscreen === region.modelData.active ? "corners-in" : "corners-out"; size: 14; color: theme.textMuted }
                }
            }
            MouseArea {
                id: handleArea
                // A 5 px strip is easy to miss: the grab area reaches a few
                // pixels past the divider on both sides.
                anchors.fill: parent
                anchors.margins: region.modelData.type === "handle" ? -3 : 0
                enabled: region.modelData.type === "handle"
                visible: enabled
                hoverEnabled: true
                cursorShape: region.modelData.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
                property real proposed: region.modelData.ratio || .5
                onPositionChanged: function(event) {
                    if (!pressed) return
                    const at = mapToItem(workspace, event.x, event.y)
                    proposed = ((region.modelData.horizontal ? at.x : at.y) - region.modelData.origin)
                             / Math.max(1, region.modelData.available)
                    resizeLine.visible = true
                    resizeLine.x = region.modelData.horizontal ? at.x : region.x
                    resizeLine.y = region.modelData.horizontal ? region.y : at.y
                    resizeLine.width = region.modelData.horizontal ? 3 : region.width
                    resizeLine.height = region.modelData.horizontal ? 3 : region.height
                }
                onReleased: { resizeLine.visible = false; workspace.state.setRatio(region.modelData.path, proposed) }
                onPressed: proposed = region.modelData.ratio
                onCanceled: resizeLine.visible = false
            }
        }
    }
    Rectangle { id: resizeLine; visible: false; z: 50; color: theme.accent }
    // Preview and a PH compass over the target. Centre means merge into tabs.
    Repeater {
        model: workspace.dragged && workspace.target ? workspace.geometry : []
        delegate: Item {
            id: dropPreview
            required property var modelData
            visible: modelData.type === "tabs" && (modelData.panes || []).indexOf(workspace.target) >= 0
            x: modelData.x; y: modelData.y
            width: modelData.width; height: modelData.height
            z: 100
            Rectangle {
                x: workspace.edge === "right" ? parent.width / 2 : 0
                y: workspace.edge === "bottom" ? parent.height / 2 : 0
                width: workspace.edge === "left" || workspace.edge === "right" ? parent.width / 2 : parent.width
                height: workspace.edge === "top" || workspace.edge === "bottom" ? parent.height / 2 : parent.height
                color: theme.accent; opacity: .18
                border.color: theme.accent; border.width: 2
            }
            Rectangle {
                anchors.centerIn: parent
                width: 112; height: 112; radius: 6
                color: theme.surface; border.color: theme.accent
                Repeater {
                    model: [{e:"top",i:"arrow-up",x:40,y:4}, {e:"bottom",i:"arrow-down",x:40,y:76},
                            {e:"left",i:"arrow-left",x:4,y:40}, {e:"right",i:"arrow-right",x:76,y:40},
                            {e:"center",i:"squares-four",x:40,y:40}]
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.x; y: modelData.y; width: 32; height: 32; radius: 3
                        color: workspace.edge === modelData.e ? theme.selection : "transparent"
                        Glyph { anchors.centerIn: parent; iconName: dropPreview.modelData.i; size: 17; color: theme.accent }
                    }
                }
            }
        }
    }
}
