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
    // The height follows the content: a fixed one left the buttons outside
    // the frame whenever a row was added.
    anchors.centerIn: Overlay.overlay

    ListModel { id: draftExcluded }

    onOpened: {
        // The four scan switches belong to WinShun's settings page when this
        // window is a plugin: the host is where they are shown and changed.
        if (!hostManagedSettings) {
            modeBox.currentIndex = scanSettings.scanMode
            workersBox.currentIndex = scanSettings.workers === 0 ? 0 : [1, 2, 4, 8].indexOf(scanSettings.workers) + 1
            mediaBox.currentIndex = scanSettings.media
            previewBox.currentIndex = scanSettings.previewIntervalMs > 1500 ? 1 : 0
            const sizes = [0, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024]
            smallFilesBox.currentIndex = Math.max(0, sizes.indexOf(scanSettings.smallFileThreshold))
        }
        draftExcluded.clear()
        const saved = scanSettings.excludedPaths
        for (let i = 0; i < saved.length; ++i) {
            draftExcluded.append({ display: saved[i] })
        }
    }
    onAccepted: {
        // The four scan switches belong to WinShun's settings page when this
        // window is a plugin: writing them here would only diverge.
        if (!hostManagedSettings) {
            const values = [0, 1, 2, 4, 8]
            scanSettings.scanMode = modeBox.currentIndex
            scanSettings.workers = values[workersBox.currentIndex]
            scanSettings.media = mediaBox.currentIndex
            scanSettings.previewIntervalMs = previewBox.currentIndex === 1 ? 5000 : 1500
            const sizes = [0, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024]
            scanSettings.smallFileThreshold = sizes[smallFilesBox.currentIndex] || 0
        }
        const paths = []
        for (let i = 0; i < draftExcluded.count; ++i) {
            paths.push(draftExcluded.get(i).display)
        }
        scanSettings.excludedPaths = paths
        scanSettings.save()
        // The settings only mean something for the next scan, so the next scan
        // is started right away when the user asked for it.
        if (rescanCheck.checked && scanSettings.lastPath.length > 0) {
            scanService.rescan()
        }
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
        id: content
        width: root.availableWidth
        // No anchors.fill here: the dialog takes the height from the content,
        // so the footer can never end up outside the frame.
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            // When WinShun owns these (its plugin settings page), the dialog
            // leaves them out instead of offering a second place to edit them.
            Label { text: "扫描方式"; color: theme.text; visible: !hostManagedSettings }
            ComboBox {
                id: modeBox
                visible: !hostManagedSettings
                Layout.fillWidth: true
                // The fast scan reads the NTFS MFT through a helper that asks
                // for administrator rights; it only works on a fixed NTFS
                // drive root.
                model: ["自动", "快扫（MFT，需管理员）", "普通遍历"]
            }

            Label { text: "普通扫描并发上限"; color: theme.text; visible: !hostManagedSettings }
            ComboBox {
                id: workersBox
                visible: !hostManagedSettings
                Layout.fillWidth: true
                // The same choices the native plugin offers: more workers are
                // not promised to be faster.
                model: ["自动", "1", "2", "4", "8"]
            }

            Label { text: "介质策略"; color: theme.text; visible: !hostManagedSettings }
            ComboBox {
                id: mediaBox
                visible: !hostManagedSettings
                Layout.fillWidth: true
                model: ["自动检测", "SSD / NVMe", "机械硬盘", "网络目录"]
            }

            Label { text: "预览发布"; color: theme.text; visible: !hostManagedSettings }
            ComboBox {
                id: previewBox
                visible: !hostManagedSettings
                Layout.fillWidth: true
                model: ["平衡", "更少更新"]
            }

            // Files below this are folded into one row per folder, which is
            // the biggest memory saving on a volume with millions of files.
            Label { text: "小文件忽略"; color: theme.text; visible: !hostManagedSettings }
            ThemedComboBox {
                id: smallFilesBox
                visible: !hostManagedSettings
                Layout.fillWidth: true
                model: ["不忽略", "16 KB", "64 KB", "256 KB", "1 MB"]
            }
        }

        Label {
            Layout.fillWidth: true
            visible: hostManagedSettings
            text: "扫描方式、并发、介质与预览频率在 WinShun 的插件设置里修改。"
            color: theme.textMuted; font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        Label { text: "排除路径"; color: theme.text; font.weight: Font.DemiBold }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            color: theme.surface
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
                        color: theme.text
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
                color: theme.textMuted
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

        CheckBox {
            id: rescanCheck
            Layout.fillWidth: true
            visible: scanSettings.lastPath.length > 0
            checked: true
            text: "确定后立即重新扫描（" + scanSettings.lastPath + "）"
        }

        Label {
            Layout.fillWidth: true
            text: "设置只影响下一次扫描，当前结果不变。保存在 " + scanSettings.filePath
            color: theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
    }
}
