import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The scan settings: the same ones the native plugin keeps in its data
// directory. The dialog edits a draft; nothing reaches the settings object
// until the user accepts, so cancelling really cancels.
Dialog {
    id: root

    title: "扫描设置"
    modal: true
    width: 480
    height: 440
    anchors.centerIn: Overlay.overlay

    ListModel { id: draftExcluded }

    onOpened: {
        workersBox.currentIndex = scanSettings.workers === 0 ? 0 : [1, 2, 4, 8].indexOf(scanSettings.workers) + 1
        mediaBox.currentIndex = scanSettings.media
        previewBox.currentIndex = scanSettings.previewIntervalMs > 1500 ? 1 : 0
        draftExcluded.clear()
        const saved = scanSettings.excludedPaths
        for (let i = 0; i < saved.length; ++i) {
            draftExcluded.append({ display: saved[i] })
        }
    }
    onAccepted: {
        const values = [0, 1, 2, 4, 8]
        scanSettings.workers = values[workersBox.currentIndex]
        scanSettings.media = mediaBox.currentIndex
        scanSettings.previewIntervalMs = previewBox.currentIndex === 1 ? 5000 : 1500
        const paths = []
        for (let i = 0; i < draftExcluded.count; ++i) {
            paths.push(draftExcluded.get(i).display)
        }
        scanSettings.excludedPaths = paths
        scanSettings.save()
    }

    standardButtons: Dialog.NoButton
    // The rest of the window is Chinese, so the buttons are too: the system
    // locale would name them in English.
    footer: RowLayout {
        spacing: 8
        Item { Layout.fillWidth: true }
        Button {
            text: "取消"
            onClicked: root.reject()
        }
        Button {
            text: "确定"
            onClicked: root.accept()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            Label { text: "普通扫描并发上限"; color: "#243246" }
            ComboBox {
                id: workersBox
                Layout.fillWidth: true
                // The same choices the native plugin offers: more workers are
                // not promised to be faster.
                model: ["自动", "1", "2", "4", "8"]
            }

            Label { text: "介质策略"; color: "#243246" }
            ComboBox {
                id: mediaBox
                Layout.fillWidth: true
                model: ["自动检测", "SSD / NVMe", "机械硬盘", "网络目录"]
            }

            Label { text: "预览发布"; color: "#243246" }
            ComboBox {
                id: previewBox
                Layout.fillWidth: true
                model: ["平衡", "更少更新"]
            }
        }

        Label { text: "排除路径"; color: "#243246"; font.weight: Font.DemiBold }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f7f9fb"
            border.color: "#dce2e8"
            radius: 4
            ListView {
                id: excludedList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                spacing: 2
                model: draftExcluded
                delegate: RowLayout {
                    width: excludedList.width
                    Label {
                        Layout.fillWidth: true
                        text: model.display
                        elide: Text.ElideMiddle
                        color: "#243246"
                        font.pixelSize: 12
                    }
                    Button {
                        text: "移除"
                        onClicked: draftExcluded.remove(index)
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: draftExcluded.count === 0
                text: "没有排除路径"
                color: "#8b98a9"
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: excludedInput
                Layout.fillWidth: true
                placeholderText: "要排除的路径，例如 D:\\工作\\node_modules"
                selectByMouse: true
            }
            Button {
                text: "添加"
                enabled: excludedInput.text.trim().length > 0
                onClicked: {
                    draftExcluded.append({ display: excludedInput.text.trim() })
                    excludedInput.text = ""
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: "设置只影响下一次扫描，当前结果不变。保存在 " + scanSettings.filePath
            color: "#66758a"
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
    }
}
