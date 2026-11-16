import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

Popup {
    id: popup

    property Item anchorItem
    property var rows: []
    property int selectedIndex: -1
    signal chosen(var entry)
    signal hovered(int index)

    parent: anchorItem
    x: 0
    y: anchorItem ? anchorItem.height + 2 : 0
    width: anchorItem ? anchorItem.width : 360
    padding: 4
    focus: false
    closePolicy: Popup.CloseOnPressOutside
    visible: anchorItem && anchorItem.activeFocus && rows.length > 0

    background: Rectangle {
        radius: Metrics.radius
        color: theme.menuBackground
        border.width: 1
        border.color: theme.menuEdge
    }

    contentItem: ListView {
        id: list
        objectName: "pathSuggestionList"
        clip: true
        implicitHeight: Math.min(contentHeight, Metrics.fileRow * 8)
        model: popup.rows
        currentIndex: popup.selectedIndex
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: StyledScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        delegate: Item {
            id: row
            required property var modelData
            required property int index
            objectName: "pathSuggestionRow:" + index
            width: ListView.view.width
            height: Metrics.fileRow
            property bool hot: hover.hovered

            Rectangle {
                anchors.fill: parent
                radius: Math.max(0, Metrics.radius - 1)
                color: row.index === popup.selectedIndex ? theme.selection
                     : row.hot ? theme.menuHover : "transparent"
            }
            HoverHandler {
                id: hover
                onHoveredChanged: if (hovered) popup.hovered(row.index)
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: Metrics.gap
                Glyph {
                    iconName: row.modelData.dir ? "folder" : "file"
                    size: Metrics.icon
                    color: theme.textMuted
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name && row.modelData.name.length ? row.modelData.name : row.modelData.path
                        font.family: Metrics.fontFamily
                        font.pixelSize: Metrics.caption
                        color: theme.text
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.detail && row.modelData.detail.length ? row.modelData.detail
                             : (row.modelData.dir ? row.modelData.path : row.modelData.parent)
                        font.family: Metrics.fontFamily
                        font.pixelSize: Metrics.small
                        color: theme.textMuted
                        elide: Text.ElideMiddle
                    }
                }
            }
            TapHandler {
                onTapped: popup.chosen(row.modelData)
            }
        }
    }

    function keepIndexVisible() {
        if (selectedIndex >= 0 && selectedIndex < list.count)
            list.positionViewAtIndex(selectedIndex, ListView.Contain)
    }
}
