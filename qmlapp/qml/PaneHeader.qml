import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property string title
    required property string iconName
    property bool maximized: false
    property bool collapsed: false
    // The dock workspace already names the pane on its tab, so the bar is
    // redundant there: `chrome: false` collapses it to nothing.
    property bool chrome: true
    property bool showTitle: true
    signal maximizeRequested()
    signal collapseRequested()
    implicitHeight: root.chrome ? metrics.paneHeader : 0
    visible: root.chrome
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
