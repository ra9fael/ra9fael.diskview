pragma Singleton
import QtQuick

// One compact scale for every panel. The host's appearance stays authoritative;
// these aliases only give the QML a shared, legible vocabulary.
QtObject {
    readonly property var t: typeof theme !== "undefined" ? theme : null
    readonly property int localFont: typeof scanSettings !== "undefined" ? scanSettings.fontSize : 13
    readonly property int body: localFont
    readonly property int caption: Math.max(11, localFont - 1)
    readonly property int small: Math.max(10, localFont - 2)
    readonly property int row: typeof scanSettings !== "undefined" ? scanSettings.treeRowHeight : 26
    readonly property int fileRow: typeof scanSettings !== "undefined" ? scanSettings.fileRowHeight : 28
    readonly property int headerRow: typeof scanSettings !== "undefined" ? scanSettings.headerRowHeight : 26
    readonly property int control: t ? t.controlHeight : 32
    readonly property int paneHeader: Math.max(headerRow, control - 4, 24)
    readonly property int icon: t ? t.iconSize : 18
    readonly property int gap: t ? t.gap : 8
    readonly property int pad: t ? t.pad : 14
    readonly property int radius: t ? t.radius : 4
    readonly property real cardRadius: t ? t.cardRadius : 8
    readonly property real controlRadius: t ? t.controlRadius : 6
    readonly property real smallRadius: t ? t.smallRadius : 4
    readonly property string fontFamily: t ? t.fontFamily : "Segoe UI"
}
