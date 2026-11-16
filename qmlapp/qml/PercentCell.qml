import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// The "% 父级" cell of a table. Both the tree and the file list use this one
// item, so a folder's share is drawn with the same bar in the same colour in
// both: the bar is the system accent at a low alpha, and its width is the
// share itself. Keeping it in one place is the only way the two tables cannot
// drift apart again.
Item {
    id: cell
    required property real percent

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // A share below one percent still gets a hairline, so the column
        // never looks empty for a row that does have a value.
        width: Math.max(cell.percent > 0 ? 2 : 0,
                        Math.min(1, cell.percent / 100) * parent.width)
        color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, theme.dark ? 0.30 : 0.18)
    }
    Label {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        verticalAlignment: Text.AlignVCenter
        text: cell.percent.toFixed(1) + "%"
        color: theme.text
        font.pixelSize: Metrics.small
    }
}
