pragma ComponentBehavior: Bound
import QtQuick
import DiskViewPrototype

// A function icon by its Phosphor name ("squares-four", "arrow-counter-clockwise"),
// drawn by the same PhosphorGlyph the main app draws its icons with: the
// outline filled into an image, the em's centre on a pixel centre or edge, so
// a symmetric icon is symmetric in pixels. A name outside the bundled set
// draws nothing at all — never a wrong character from another font.
Item {
    id: icon

    property string iconName
    // The Fill weight, for an "on" state.
    property bool filled: false
    property real size: 16
    property color color: theme.text
    // A pressed icon, a little smaller: the outline drawn afresh at a whole
    // number of device pixels, never a scaled picture.
    property real inkScale: 1

    readonly property var resolved: theme.phosphorIcon(iconName, filled)

    implicitWidth: size
    implicitHeight: size

    PhosphorGlyph {
        anchors.centerIn: parent
        readonly property bool used: !!icon.resolved
        visible: used
        text: used ? icon.resolved.text : ""
        family: used ? icon.resolved.family : ""
        color: icon.color
        em: icon.size
        inkScale: icon.inkScale
        box: Qt.size(icon.width, icon.height)
    }
}
