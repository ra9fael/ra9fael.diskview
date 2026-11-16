import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

// The existing panes are placed, not instantiated. Moving a tab therefore
// preserves chart transforms, model selection and list scroll positions.
Item {
    id: workspace
    required property var panes
    required property var state
    property string fullscreen: ""
    property var geometry: []
    property string dragged: ""
    property string target: ""
    property string edge: ""
    property point pointer
    property bool showHeaders: true
    property var collapsedGroups: ({})
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
    function refresh() {
        if (!visible || !width || !height) return
        geometry = state.placements(width, height)
        for (const id of ["tree", "files", "legend", "chart"]) {
            const pane = panes[id]
            if (fullscreen === id) continue
            pane.parent = content
            pane.visible = false
            pane.clip = true
        }
        for (const slot of geometry) {
            if (slot.type !== "tabs") continue
            const pane = panes[slot.active]
            if (fullscreen === slot.active) continue
            pane.x = slot.x
            pane.y = slot.y + Metrics.row
            pane.width = slot.width
            pane.height = groupCollapsed(slot.active) ? 0 : Math.max(0, slot.height - Metrics.row)
            pane.visible = !groupCollapsed(slot.active)
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
            target = slot.active
            const panesInGroup = slot.panes || []
            if (target === dragged) {
                for (const candidate of panesInGroup) {
                    if (candidate !== dragged) { target = candidate; break }
                }
            }
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
    onWidthChanged: refresh()
    onHeightChanged: refresh()
    onVisibleChanged: if (visible) Qt.callLater(refresh)
    Connections {
        target: workspace.state
        function onChanged() { workspace.refresh(); workspace.persisted() }
    }

    Rectangle { anchors.fill: parent; color: theme.window }
    Item { id: content; anchors.fill: parent }
    Repeater {
        model: workspace.geometry
        delegate: Item {
            id: region
            required property var modelData
            objectName: modelData.type === "handle" ? "dock-handle-" + modelData.path : "dock-group-" + modelData.active
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.type === "tabs" ? Metrics.row : modelData.height
            z: 2
            Rectangle {
                anchors.fill: parent
                color: theme.surfaceAlt
                border.color: theme.border
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
                    implicitWidth: Metrics.row; implicitHeight: Metrics.row
                    ToolTip.visible: hovered
                    ToolTip.text: workspace.groupCollapsed(region.modelData.active) ? "展开" : "折叠"
                    onClicked: workspace.toggleGroup(region.modelData.active)
                    contentItem: Glyph { iconName: workspace.groupCollapsed(region.modelData.active) ? "plus" : "minus"; size: 14; color: theme.textMuted }
                }
                ToolButton {
                    implicitWidth: Metrics.row; implicitHeight: Metrics.row
                    ToolTip.visible: hovered
                    ToolTip.text: workspace.fullscreen === region.modelData.active ? "还原" : "最大化"
                    onClicked: workspace.maximizeRequested(region.modelData.active)
                    contentItem: Glyph { iconName: workspace.fullscreen === region.modelData.active ? "corners-in" : "corners-out"; size: 14; color: theme.textMuted }
                }
                Label { visible: workspace.showHeaders; text: workspace.title(region.modelData.active); font.family: Metrics.fontFamily; font.pixelSize: Metrics.caption; color: theme.textMuted; Layout.preferredWidth: 80 }
            }
            MouseArea {
                anchors.fill: parent
                enabled: region.modelData.type === "handle"
                visible: enabled
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
                    resizeLine.height = region.modelData.horizontal ? region.height : 3
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
                        Glyph { anchors.centerIn: parent; iconName: modelData.i; size: 17; color: theme.accent }
                    }
                }
            }
        }
    }
}
