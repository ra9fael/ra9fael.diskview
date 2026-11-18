import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

// The settings, in one place: the viewer's dialog and the plugin's own
// settings window show the same fields. Everything here is a draft until it is
// applied; cancelling leaves the stored settings alone, and applying never
// starts a scan.
ColumnLayout {
    id: root

    // The host owns the first three: they are sent back to it, which stores
    // them and answers with the merged values. A standalone run has no one to
    // send them to and keeps them in the local file.
    readonly property bool hosted: typeof hostBridge !== "undefined" && hostBridge !== null
    // The viewer's dialog can rescan; the settings window has no scanner.
    readonly property bool canRescan: typeof scanService !== "undefined" && scanService !== null

    signal applied()
    signal cancelled()

    spacing: 12

    // Fills the fields from the stored settings. Called when the page is shown.
    function begin() {
        viewBox.currentIndex = scanSettings.defaultView
        modeBox.currentIndex = scanSettings.scanMode
        folderMenuCheck.checked = scanSettings.folderMenuEnabled
        workersBox.currentIndex = scanSettings.workers === 0 ? 0 : [1, 2, 4, 8].indexOf(scanSettings.workers) + 1
        mediaBox.currentIndex = scanSettings.media
        previewBox.currentIndex = scanSettings.previewIntervalMs > 1500 ? 1 : 0
        const sizes = [0, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024]
        smallFilesBox.currentIndex = Math.max(0, sizes.indexOf(scanSettings.smallFileThreshold))
        draftExcluded.clear()
        const saved = scanSettings.excludedPaths
        for (let i = 0; i < saved.length; ++i) {
            draftExcluded.append({ display: saved[i] })
        }
        status.text = ""
    }

    // Writes the edited fields and nothing else. The public three go to the
    // host as well, so its card and the next window show the same values.
    function apply() {
        const view = viewBox.currentIndex
        const mode = modeBox.currentIndex
        const folderMenu = folderMenuCheck.checked
        scanSettings.defaultView = view
        scanSettings.scanMode = mode
        scanSettings.folderMenuEnabled = folderMenu
        const values = [0, 1, 2, 4, 8]
        scanSettings.workers = values[workersBox.currentIndex]
        scanSettings.media = mediaBox.currentIndex
        scanSettings.previewIntervalMs = previewBox.currentIndex === 1 ? 5000 : 1500
        const sizes = [0, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024]
        scanSettings.smallFileThreshold = sizes[smallFilesBox.currentIndex] || 0
        const paths = []
        for (let i = 0; i < draftExcluded.count; ++i) {
            paths.push(draftExcluded.get(i).display)
        }
        scanSettings.excludedPaths = paths
        if (!scanSettings.save()) {
            status.text = "设置没有写入 " + scanSettings.filePath
            return false
        }
        if (root.hosted) {
            hostBridge.applyPublic({
                defaultView: view === 1 ? "sunburst" : "treemap",
                scanMode: ["auto", "fast", "ordinary"][mode],
                folderMenuEnabled: folderMenu
            })
        }
        status.text = "已保存，下一次扫描生效"
        return true
    }

    ListModel { id: draftExcluded }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 10

        Label { text: "默认视图"; color: theme.text }
        ThemedComboBox {
            id: viewBox
            Layout.fillWidth: true
            // What the next window opens with. Switching the layout while
            // browsing does not change it.
            model: ["方块图", "圆盘图"]
        }

        Label { text: "扫描方式"; color: theme.text }
        ThemedComboBox {
            id: modeBox
            Layout.fillWidth: true
            // The fast scan reads the NTFS MFT through a helper that asks for
            // administrator rights; it only works on a fixed NTFS drive root.
            model: ["自动", "快扫（MFT，需管理员）", "普通遍历"]
        }

        Label { text: "文件夹右键菜单"; color: theme.text }
        CheckBox {
            id: folderMenuCheck
            Layout.fillWidth: true
            text: "在文件夹结果上提供「分析文件夹占用」"
            font.pixelSize: theme.fontSize
        }

        Label { text: "普通扫描并发上限"; color: theme.text }
        ThemedComboBox {
            id: workersBox
            Layout.fillWidth: true
            // The same choices the native plugin offers: more workers are not
            // promised to be faster.
            model: ["自动", "1", "2", "4", "8"]
        }

        Label { text: "介质策略"; color: theme.text }
        ThemedComboBox {
            id: mediaBox
            Layout.fillWidth: true
            model: ["自动检测", "SSD / NVMe", "机械硬盘", "网络目录"]
        }

        Label { text: "预览发布"; color: theme.text }
        ThemedComboBox {
            id: previewBox
            Layout.fillWidth: true
            model: ["平衡", "更少更新"]
        }

        // Files below this are folded into one row per folder, which is the
        // biggest memory saving on a volume with millions of files.
        Label { text: "小文件聚合"; color: theme.text }
        ThemedComboBox {
            id: smallFilesBox
            Layout.fillWidth: true
            model: ["不聚合", "16 KB", "64 KB", "256 KB", "1 MB"]
        }
    }

    Label { text: "排除路径"; color: theme.text; font.weight: Font.DemiBold }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        color: theme.surface
        border.color: theme.border
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

    Label {
        id: status
        Layout.fillWidth: true
        text: ""
        color: theme.textMuted
        font.pixelSize: 11
        elide: Text.ElideMiddle
    }

    Label {
        Layout.fillWidth: true
        text: "设置只影响下一次扫描，当前结果不变。保存在 " + scanSettings.filePath
        color: theme.textMuted
        font.pixelSize: 11
        elide: Text.ElideMiddle
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 2
        spacing: 8
        // Rescanning is its own action: applying the settings never starts a
        // scan, and this one needs a target and an idle scanner.
        ThemedButton {
            visible: root.canRescan
            iconName: "arrows-clockwise"
            text: "用新设置重新扫描"
            enabled: root.canRescan && scanSettings.lastPath.length > 0 && !scanService.isRunning()
            onClicked: {
                if (root.apply()) scanService.rescan()
            }
        }
        Item { Layout.fillWidth: true }
        ThemedButton {
            text: "取消"
            compactHeight: 30
            Layout.minimumWidth: 64
            onClicked: root.cancelled()
        }
        ThemedButton {
            objectName: "applySettings"
            text: "应用"
            primary: true
            compactHeight: 30
            Layout.minimumWidth: 72
            onClicked: { if (root.apply()) root.applied() }
        }
    }
}
