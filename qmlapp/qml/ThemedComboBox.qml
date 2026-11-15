import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// A ComboBox drawn from the same tokens as the rest of the window. The Basic
// style's default indicator is a large triangle that matches nothing here; this
// one is a 12 px Phosphor caret, the host's control colours and radii, and a
// popup in the menu surface.
ComboBox {
    id: box

    implicitHeight: 26
    font.pixelSize: theme.fontSize

    background: Rectangle {
        radius: theme.radius
        color: box.visualFocus ? theme.surface : theme.surfaceAlt
        border.width: 1
        border.color: box.visualFocus ? theme.accent : theme.border
    }

    contentItem: Label {
        leftPadding: 9
        rightPadding: box.indicator.width + 8
        text: box.displayText
        color: theme.text
        font.pixelSize: theme.fontSize
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Glyph {
        x: box.width - width - 7
        y: (box.height - height) / 2
        iconName: "caret-down"
        size: 12
        color: theme.textMuted
    }

    delegate: ItemDelegate {
        id: entry
        required property var modelData
        required property int index
        width: box.width
        height: 26
        highlighted: box.highlightedIndex === index
        background: Rectangle {
            radius: theme.radius
            color: entry.highlighted ? theme.hoverRow : "transparent"
        }
        contentItem: Label {
            leftPadding: 8
            text: entry.modelData
            color: theme.text
            font.pixelSize: theme.fontSize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: box.height + 4
        width: box.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        background: Rectangle {
            radius: theme.radius
            color: theme.surface
            border.width: 1
            border.color: theme.border
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
