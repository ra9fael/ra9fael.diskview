import QtQuick
import QtQuick.Controls
import DiskViewPrototype

// The viewer's settings dialog. The fields are the shared settings content;
// the dialog only adds the frame. The plugin's own settings window shows the
// same fields.
Dialog {
    id: root

    title: "扫描设置"
    modal: true
    width: 480
    // The height follows the content: a fixed one left the buttons outside
    // the frame whenever a row was added.
    anchors.centerIn: Overlay.overlay

    standardButtons: Dialog.NoButton

    SettingsContent {
        id: content
        width: root.availableWidth
        // The buttons live inside the content rather than in a custom footer:
        // the footer is docked to the popup's bottom edge without being part
        // of its implicit height, and the buttons ended up over the frame.
        onApplied: root.accept()
        onCancelled: root.reject()
    }

    onOpened: content.begin()
}
