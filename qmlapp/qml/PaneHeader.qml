import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property string title
    required property string iconName
    property bool maximized: false
    property bool collapsed: false
    property bool showFilter: false
    property bool showColumnsButton: false
    // Dock mode already names the pane on its tab, so the header keeps only
    // the controls and leaves the title out.
    property bool showTitle: true
    property alias filterField: filterInput
    signal maximizeRequested()
    signal collapseRequested()
    signal columnsRequested(Item anchor)
    implicitHeight: metrics.paneHeader
    clip: true

    Rectangle {
        anchors.fill: parent
        color: theme.surfaceAlt
        border.color: theme.border
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: root.maximizeRequested()
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: metrics.pad / 2
        anchors.rightMargin: metrics.pad / 2
        spacing: 4
        Glyph { visible: root.showTitle; iconName: root.iconName; size: metrics.icon; color: theme.accent }
        Label {
            visible: root.showTitle
            Layout.fillWidth: true
            text: root.title
            color: theme.text
            font.pixelSize: metrics.caption
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        TextField {
            id: filterInput
            visible: root.showFilter
            objectName: "filterField"
            Layout.preferredWidth: 180
            Layout.fillWidth: true
            Layout.minimumWidth: 64
            Layout.maximumWidth: 200
            Layout.preferredHeight: Math.max(20, metrics.paneHeader - 6)
            topPadding: 2
            bottomPadding: 2
            font.pixelSize: metrics.small
            placeholderText: "筛选名称或文件…"
            selectByMouse: true
            onTextEdited: fileListModel.filter = text
        }
        ToolButton {
            id: columnsButton
            visible: root.showColumnsButton
            implicitWidth: Math.min(metrics.control, metrics.paneHeader - 4)
            implicitHeight: Math.max(20, metrics.paneHeader - 6)
            ToolTip.visible: hovered
            ToolTip.text: "显示或隐藏列"
            onClicked: root.columnsRequested(columnsButton)
            contentItem: Glyph { iconName: "list-numbers"; size: 15; color: theme.text }
        }
        ToolButton {
            implicitWidth: Math.min(metrics.control, metrics.paneHeader - 4)
            implicitHeight: Math.max(20, metrics.paneHeader - 6)
            ToolTip.visible: hovered
            ToolTip.text: root.collapsed ? "展开" : "折叠"
            onClicked: root.collapseRequested()
            contentItem: Glyph { iconName: root.collapsed ? "plus" : "minus"; size: metrics.small; color: theme.textMuted }
        }
        ToolButton {
            implicitWidth: Math.min(metrics.control, metrics.paneHeader - 4)
            implicitHeight: Math.max(20, metrics.paneHeader - 6)
            ToolTip.visible: hovered
            ToolTip.text: root.maximized ? "还原" : "最大化"
            onClicked: root.maximizeRequested()
            contentItem: Glyph { iconName: root.maximized ? "corners-in" : "corners-out"; size: metrics.small; color: theme.textMuted }
        }
    }
}
