import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// A ComboBox drawn from the same tokens as the rest of the window. The Basic
// style's default indicator is a large triangle that matches nothing here; this
// one is a 12 px Phosphor caret, the host's control colours and radii, and a
// popup in the menu surface.
//
// The box is as wide as its widest entry and the popup as wide as its widest
// row, so a short choice is not cut off and a long one does not stretch the
// toolbar.
ComboBox {
    id: box

    implicitHeight: 26
    font.pixelSize: theme.fontSize

    // The text of entry i, as the box shows it.
    function entryText(i) {
        const item = box.model[i]
        if (item === undefined || item === null) return ""
        return box.textRole && typeof item === "object" ? String(item[box.textRole]) : String(item)
    }
    // A hidden Text measures the widest entry. Its implicitWidth is reliable
    // and reactive, unlike FontMetrics, which answers 0 until long after the
    // first layout and used to leave the box collapsed and its text elided.
    property string widestEntry: ""
    Text {
        id: textMeasure
        opacity: 0
        x: -10000; y: -10000
        font: box.font
        text: box.widestEntry
    }
    function measure() {
        let best = box.displayText || ""
        const entries = box.model
        const count = entries && entries.length !== undefined ? entries.length : 0
        for (let i = 0; i < count; ++i) {
            const t = entryText(i)
            if (t.length > best.length) best = t
        }
        widestEntry = best
    }
    onModelChanged: measure()
    onDisplayTextChanged: measure()
    Component.onCompleted: measure()
    readonly property real widestText: textMeasure.implicitWidth
    // A popup row's width beyond the box; a delegate with more in a row
    // raises it.
    property real popupContentWidth: widestText + 16

    implicitWidth: widestText + 9 + indicator.width + 8 + 2

    // The Basic style pads its control on both sides by default. Our label
    // carries its own padding, so without this the two stack and eat the whole
    // text area (a short entry shows "..." even though the box is wide enough).
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    background: Rectangle {
        radius: theme.controlRadius
        // The control fill, not a surface: the Basic style reads
        // palette.base, and on Mica the surfaces are see-through, so the box
        // needs the same solid control colour WinShun's own field carries.
        color: theme.control
        border.width: 1
        border.color: box.visualFocus ? theme.accent : theme.controlBorder
    }

    contentItem: Label {
        leftPadding: 9
        rightPadding: box.indicator.width + 8
        text: box.displayText
        color: theme.text
        font.pixelSize: box.font.pixelSize
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
        width: ListView.view ? ListView.view.width : box.width
        height: 26
        highlighted: box.highlightedIndex === index
        background: Rectangle {
            radius: theme.radius
            color: entry.highlighted ? theme.hoverRow : "transparent"
        }
        contentItem: Label {
            leftPadding: 8
            text: box.entryText(entry.index)
            color: theme.text
            font.pixelSize: box.font.pixelSize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: box.height + 4
        width: Math.max(box.width, box.popupContentWidth + leftPadding + rightPadding)
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
            ScrollBar.vertical: StyledScrollBar {}
        }
    }
}
