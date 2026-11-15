import QtQuick

// The column widths of one table, by key. The spec in Main.qml supplies the
// default and the minimum; this object holds only what the user dragged, so a
// reset drops a key and falls back to the spec. The map is replaced on every
// change, which is what re-evaluates the bindings that read a width.
QtObject {
    id: store

    // key -> width, for the columns the user has dragged.
    property var widths: ({})
    // key -> default width, filled from the spec once at startup.
    property var defaults: ({})
    // Raised after any change; Main.qml persists the three stores together.
    signal changed()

    function width(key) {
        const value = store.widths[key]
        return value === undefined ? (store.defaults[key] || 60) : value
    }
    function setWidth(key, value) {
        if (store.widths[key] === value) return
        const next = Object.assign({}, store.widths)
        next[key] = value
        store.widths = next
        store.changed()
    }
    function reset(key) {
        if (store.widths[key] === undefined) return
        const next = Object.assign({}, store.widths)
        delete next[key]
        store.widths = next
        store.changed()
    }
    function resetAll() {
        store.widths = ({})
        store.changed()
    }
}
