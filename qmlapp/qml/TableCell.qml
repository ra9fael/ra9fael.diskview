import QtQuick
import QtQuick.Layouts

// One cell of a table header or row: the column's width, a hairline on its
// right edge, and whatever the caller declares inside. The row draws the line
// underneath once, so a row reads as one band with vertical rules instead of a
// stack of boxes. Content insets itself (the labels carry their own margin),
// which keeps the rule clear of the text.
Item {
    id: cell
    clip: true

    property int cellWidth: 60
    property color lineColor: "transparent"

    default property alias content: inner.data

    implicitWidth: cellWidth
    implicitHeight: 24
    Layout.preferredWidth: cellWidth
    Layout.minimumWidth: 0
    Layout.fillHeight: true

    Item {
        id: inner
        anchors.fill: parent

        // First child, so the cell's own content is painted over the rule.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: cell.lineColor
        }
    }
}
