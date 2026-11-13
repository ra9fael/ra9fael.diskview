import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

ApplicationWindow {
    id: window
    width: 1280
    height: 840
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "Disk View · QML 技术样机"
    color: "#f4f6f8"

    DiskViewModel { id: diskModel; objectName: "diskModel" }
    Component.onCompleted: {
        diskModel.loadFixture(fixtureSource)
        // The file view remembers how it was left.
        diskModel.fuzzyMatch = scanSettings.fuzzyMatch
        diskModel.wholeTree = scanSettings.wholeTree
    }

    // Which table columns are on. The name column is not listed: it never
    // goes away.
    function columnHidden(key) { return scanSettings.hiddenColumns.indexOf(key) >= 0 }
    function toggleColumn(key) {
        const hidden = scanSettings.hiddenColumns.slice()
        const at = hidden.indexOf(key)
        if (at >= 0) hidden.splice(at, 1)
        else hidden.push(key)
        scanSettings.hiddenColumns = hidden
        scanSettings.save()
    }

    // The breadcrumb of the current folder, shortened when it gets long the
    // way the native panel shortens it.
    property var crumbs: {
        const all = diskModel.breadcrumb
        if (all.length <= 6) return all
        return [all[0], { name: "…", id: "", current: false }].concat(all.slice(all.length - 4))
    }
    // The extension the legend picked; empty means "every colour".
    property string highlightExt: ""

    Connections {
        target: scanService
        function onStarted(path) { diskModel.clear() }
        function onPartialTree(tree) { diskModel.loadFlat(tree, true) }
        function onFinished(tree) { diskModel.loadFlat(tree, false) }
        function onCancelled() { diskModel.setPartial(true) }
        function onFailed(message) { diskModel.setPartial(true) }
    }

    Connections {
        target: diskModel
        function onSelectedChanged() {
            if (diskModel.selectedRow >= 0) list.positionViewAtIndex(diskModel.selectedRow, ListView.Contain)
        }
        function onListChanged() {
            // Navigating clears the filter in the model, so the field has to
            // follow it.
            if (filterField.text !== diskModel.filter) filterField.text = diskModel.filter
        }
        function onCurrentPathChanged() { window.highlightExt = "" }
    }

    ScanSettingsDialog { id: settingsDialog; objectName: "scanSettingsDialog" }
    ScanIssuesDialog { id: issuesDialog; objectName: "scanIssuesDialog" }

    // The read-only half of the native context menu; the file actions wait for
    // the host integration.
    Menu {
        id: contextMenu
        property string targetPath: ""
        property string targetId: ""
        MenuItem {
            text: "复制完整路径"
            enabled: contextMenu.targetPath.length > 0
            onClicked: appHost.copyToClipboard(contextMenu.targetPath)
        }
        MenuItem {
            text: "在资源管理器中显示"
            enabled: contextMenu.targetPath.length > 0
            onClicked: appHost.revealInExplorer(contextMenu.targetPath)
        }
        MenuItem {
            text: "定位到所在目录"
            enabled: contextMenu.targetId.length > 0
            onClicked: diskModel.revealNode(contextMenu.targetId)
        }
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: { filterField.forceActiveFocus(); filterField.selectAll() }
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: { pathField.forceActiveFocus(); pathField.selectAll() }
    }
    Shortcut { sequence: "F5"; onActivated: scanService.start(pathField.text.trim()) }
    Shortcut { sequence: "Alt+Left"; onActivated: diskModel.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: diskModel.goForward() }
    Shortcut { sequence: "Esc"; onActivated: { diskModel.filter = ""; filterField.text = "" } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- title row -------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "#ffffff"
            border.color: "#dce2e8"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 10
                Label { text: "Disk View"; font.pixelSize: 14; font.weight: Font.DemiBold; color: "#202b3c" }
                Label { text: "QML 技术样机"; font.pixelSize: 12; color: "#657386" }
                Item { Layout.fillWidth: true }
                Label {
                    text: scanService.state === "done" ? "真实扫描"
                          : scanService.state === "scanning" ? (scanService.paused ? "扫描已暂停" : "扫描中")
                          : scanService.state === "cancelled" ? "已取消的扫描"
                          : scanService.state === "error" ? "扫描失败"
                          : "固定快照"
                    font.pixelSize: 11
                    color: scanService.state === "done" ? "#168c84"
                          : scanService.state === "scanning" ? "#a76414"
                          : scanService.state === "error" ? "#b3261e"
                          : "#657386"
                }
                BusyIndicator { running: scanService.state === "scanning"; visible: running; implicitWidth: 14; implicitHeight: 14 }
            }
        }

        // ---- drive, path and scan controls -----------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#ffffff"
            border.color: "#dce2e8"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 8
                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    text: initialScanPath
                    placeholderText: "扫描路径，例如 D:\\"
                    selectByMouse: true
                }
                Button {
                    text: (scanService.state === "done" || scanService.state === "cancelled") ? "重新扫描" : "扫描"
                    Layout.minimumWidth: 72
                    enabled: pathField.text.trim().length > 0 && scanService.state !== "scanning"
                    onClicked: scanService.start(pathField.text.trim())
                }
                Button {
                    text: scanService.paused ? "继续" : "暂停"
                    Layout.minimumWidth: 60
                    visible: scanService.state === "scanning"
                    enabled: scanService.state === "scanning"
                    onClicked: scanService.paused ? scanService.resume() : scanService.pause()
                }
                Button { text: "取消"; Layout.minimumWidth: 60; visible: scanService.state === "scanning"; enabled: scanService.state === "scanning"; onClicked: scanService.cancel() }
                Button {
                    text: "扫描问题"
                    Layout.minimumWidth: 76
                    visible: scanService.skippedDirs > 0 || scanService.skippedLinks > 0
                          || scanService.unknownSizes > 0 || scanService.fallbackReason.length > 0
                    onClicked: issuesDialog.open()
                }
                Button { text: "扫描设置"; Layout.minimumWidth: 76; onClicked: settingsDialog.open() }
            }
        }

        // ---- view tabs, search scope and the columns menu ---------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: "#ffffff"
            border.color: "#dce2e8"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 6
                Button {
                    text: "树视图"
                    onClicked: diskModel.viewMode = 0
                    background: Rectangle {
                        radius: 3
                        color: diskModel.viewMode === 0 ? "#e3eefb" : "transparent"
                        border.color: "#dce2e8"
                    }
                }
                Button {
                    text: "文件视图"
                    onClicked: diskModel.viewMode = 1
                    background: Rectangle {
                        radius: 3
                        color: diskModel.viewMode === 1 ? "#e3eefb" : "transparent"
                        border.color: "#dce2e8"
                    }
                }
                // The file view is a file finder: say where it looks and how
                // loosely it matches.
                ComboBox {
                    visible: diskModel.viewMode === 1
                    implicitWidth: 108
                    implicitHeight: 26
                    font.pixelSize: 11
                    model: ["当前目录", "整个扫描"]
                    currentIndex: diskModel.wholeTree ? 1 : 0
                    onActivated: { diskModel.wholeTree = (index === 1); scanSettings.wholeTree = diskModel.wholeTree; scanSettings.save() }
                }
                ComboBox {
                    visible: diskModel.viewMode === 1
                    implicitWidth: 96
                    implicitHeight: 26
                    font.pixelSize: 11
                    model: ["子串匹配", "模糊匹配"]
                    currentIndex: diskModel.fuzzyMatch ? 1 : 0
                    onActivated: { diskModel.fuzzyMatch = (index === 1); scanSettings.fuzzyMatch = diskModel.fuzzyMatch; scanSettings.save() }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "列"
                    implicitWidth: 34
                    implicitHeight: 26
                    font.pixelSize: 11
                    ToolTip.visible: hovered
                    ToolTip.text: "显示或隐藏列"
                    onClicked: columnsMenu.open()
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "#33445c"
                    }
                }
                Menu {
                    id: columnsMenu
                    // The name column always stays: a table of numbers with no
                    // names is not usable.
                    Repeater {
                        model: [
                            { key: "pct", label: "% 父级" },
                            { key: "alloc", label: "已分配" },
                            { key: "items", label: "项目" },
                            { key: "files", label: "文件" },
                            { key: "dirs", label: "文件夹" },
                            { key: "modified", label: "修改时间" },
                            { key: "attributes", label: "属性" }
                        ]
                        delegate: MenuItem {
                            text: modelData.label
                            checkable: true
                            checked: !window.columnHidden(modelData.key)
                            onClicked: window.toggleColumn(modelData.key)
                        }
                    }
                }
                TextField {
                    id: filterField
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 26
                    topPadding: 4
                    bottomPadding: 4
                    font.pixelSize: 11
                    placeholderText: diskModel.viewMode === 1 ? "查找文件名…" : "筛选名称…"
                    selectByMouse: true
                    onTextEdited: diskModel.filter = text
                }
            }
        }

        // ---- tables above, the chart below ------------------------------
        SplitView {
            id: verticalSplit
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true
            handle: Rectangle { implicitHeight: 5; color: "#e6ebf2"; border.color: "#d3dbe5" }

            // folder table | extension legend
            SplitView {
                id: topSplit
                orientation: Qt.Horizontal
                SplitView.preferredHeight: 320
                SplitView.minimumHeight: 110
                handle: Rectangle { implicitWidth: 5; color: "#e6ebf2"; border.color: "#d3dbe5" }

                Item {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 430
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: "#f0f3f9"
                            border.color: "#e5eaf0"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 120
                                    text: "文件夹 / 名称" + (diskModel.sortKey === 1 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"
                                    elide: Text.ElideRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(1) }
                                }
                                Label {
                                    visible: !window.columnHidden("pct")
                                    Layout.preferredWidth: columns.pct
                                    text: "% 父级"
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    Layout.preferredWidth: columns.size
                                    text: "大小" + (diskModel.sortKey === 0 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"
                                    horizontalAlignment: Text.AlignRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(0) }
                                }
                                Label { visible: !window.columnHidden("alloc"); Layout.preferredWidth: columns.alloc; text: "已分配"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.columnHidden("items"); Layout.preferredWidth: columns.items; text: "项目"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.columnHidden("files"); Layout.preferredWidth: columns.files; text: "文件"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.columnHidden("dirs"); Layout.preferredWidth: columns.dirs; text: "文件夹"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label {
                                    visible: !window.columnHidden("modified")
                                    Layout.preferredWidth: columns.modified
                                    text: "修改时间" + (diskModel.sortKey === 2 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"
                                    horizontalAlignment: Text.AlignRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(2) }
                                }
                                Label { visible: !window.columnHidden("attributes"); Layout.preferredWidth: columns.attrs; text: "属性"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                            }
                        }
                        ListView {
                            id: list
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: diskModel
                            clip: true
                            reuseItems: true
                            boundsBehavior: Flickable.StopAtBounds
                            focus: true
                            keyNavigationEnabled: false
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Up) { diskModel.moveSelection(-1); event.accepted = true }
                                else if (event.key === Qt.Key_Down) { diskModel.moveSelection(1); event.accepted = true }
                                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { diskModel.openSelected(); event.accepted = true }
                                else if (event.key === Qt.Key_Right) { diskModel.openSelected(); event.accepted = true }
                                else if (event.key === Qt.Key_Left || event.key === Qt.Key_Backspace) { diskModel.goUp(); event.accepted = true }
                            }
                            delegate: ItemDelegate {
                                id: rowItem
                                required property string nodeId
                                required property string name
                                required property string sizeText
                                required property string allocText
                                required property string path
                                required property string extColor
                                required property string modifiedText
                                required property string attributes
                                required property real percent
                                required property int depth
                                required property int items
                                required property int files
                                required property int dirs
                                required property bool directory
                                required property bool hasChildren
                                required property bool expanded
                                width: ListView.view.width
                                height: 24
                                padding: 0
                                highlighted: nodeId === diskModel.selectedId
                                // A click on a folder row opens it in place; a
                                // double click goes into it. The triangle is
                                // still there, so nobody has to guess.
                                onClicked: {
                                    diskModel.select(nodeId)
                                    if (directory && hasChildren) diskModel.toggleExpanded(nodeId)
                                }
                                onDoubleClicked: if (directory) diskModel.navigate(nodeId)
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onSingleTapped: function(eventPoint) {
                                        contextMenu.targetId = rowItem.nodeId
                                        contextMenu.targetPath = rowItem.path
                                        contextMenu.parent = rowItem
                                        contextMenu.x = eventPoint.position.x
                                        contextMenu.y = eventPoint.position.y
                                        contextMenu.open()
                                    }
                                }
                                contentItem: RowLayout {
                                    spacing: 0
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6 + (diskModel.viewMode === 0 ? depth * 12 : 0)
                                            spacing: 4
                                            Label {
                                                Layout.preferredWidth: 12
                                                visible: rowItem.hasChildren
                                                text: rowItem.expanded ? "▾" : "▸"
                                                color: "#7b8a9d"; font.pixelSize: 10
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: diskModel.toggleExpanded(rowItem.nodeId)
                                                }
                                            }
                                            Item { visible: !rowItem.hasChildren; Layout.preferredWidth: 12 }
                                            Rectangle { Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 2; color: rowItem.extColor }
                                            Label {
                                                Layout.fillWidth: true
                                                text: rowItem.name
                                                elide: Text.ElideRight
                                                color: "#243246"; font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Item {
                                        visible: !window.columnHidden("pct")
                                        Layout.preferredWidth: columns.pct
                                        Layout.fillHeight: true
                                        Rectangle {
                                            x: 4
                                            y: parent.height / 2 - 4
                                            height: 8
                                            radius: 2
                                            color: "#cfe3f8"
                                            width: Math.max(0, Math.min(1, rowItem.percent / 100)) * (parent.width - 8)
                                        }
                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            verticalAlignment: Text.AlignVCenter
                                            text: rowItem.percent.toFixed(1) + "%"
                                            color: "#33445c"; font.pixelSize: 11
                                        }
                                    }
                                    Label { Layout.preferredWidth: columns.size; text: rowItem.sizeText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: "#33445c"; font.pixelSize: 11 }
                                    Label { visible: !window.columnHidden("alloc"); Layout.preferredWidth: columns.alloc; text: rowItem.allocText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: "#56657b"; font.pixelSize: 11 }
                                    Label {
                                        visible: !window.columnHidden("items")
                                        Layout.preferredWidth: columns.items
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.items) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: "#56657b"; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.columnHidden("files")
                                        Layout.preferredWidth: columns.files
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.files) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: "#56657b"; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.columnHidden("dirs")
                                        Layout.preferredWidth: columns.dirs
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.dirs) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: "#56657b"; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.columnHidden("modified")
                                        Layout.preferredWidth: columns.modified
                                        text: rowItem.modifiedText
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: "#56657b"; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.columnHidden("attributes")
                                        Layout.preferredWidth: columns.attrs
                                        text: rowItem.attributes
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: "#56657b"; font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    SplitView.preferredWidth: 360
                    SplitView.minimumWidth: 250
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: "#f0f3f9"
                            border.color: "#e5eaf0"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                Label { Layout.preferredWidth: 18; text: "" }
                                Label { Layout.fillWidth: true; Layout.minimumWidth: 70; text: "扩展名"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b" }
                                Label { Layout.preferredWidth: 52; text: "占比"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 66; text: "大小"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 66; text: "已分配"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 52; text: "文件数"; font.pixelSize: 11; font.weight: Font.DemiBold; color: "#56657b"; horizontalAlignment: Text.AlignRight }
                            }
                        }
                        ListView {
                            id: legend
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: diskModel.extensionRows
                            delegate: ItemDelegate {
                                width: ListView.view.width
                                height: 22
                                padding: 0
                                highlighted: window.highlightExt.length > 0 && window.highlightExt === modelData.ext
                                onClicked: {
                                    const ext = modelData.ext
                                    window.highlightExt = window.highlightExt === ext ? "" : ext
                                }
                                contentItem: RowLayout {
                                    spacing: 0
                                    Item { Layout.preferredWidth: 18; Layout.fillHeight: true
                                        Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 2; color: modelData.color }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 70
                                        text: (modelData.ext.length ? modelData.ext : "(无)") + "  " + modelData.label
                                        elide: Text.ElideRight
                                        color: "#243246"; font.pixelSize: 11
                                    }
                                    Item {
                                        Layout.preferredWidth: 52
                                        Layout.fillHeight: true
                                        Rectangle {
                                            x: 2
                                            y: parent.height / 2 - 4
                                            height: 8
                                            radius: 2
                                            color: modelData.color
                                            opacity: 0.55
                                            width: Math.max(0, Math.min(1, modelData.percent / 100)) * (parent.width - 4)
                                        }
                                        Label {
                                            anchors.fill: parent
                                            anchors.rightMargin: 2
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: modelData.percent.toFixed(1) + "%"
                                            color: "#33445c"; font.pixelSize: 11
                                        }
                                    }
                                    Label { Layout.preferredWidth: 66; text: modelData.sizeText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: "#33445c"; font.pixelSize: 11 }
                                    Label { Layout.preferredWidth: 66; text: modelData.allocText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: "#56657b"; font.pixelSize: 11 }
                                    Label { Layout.preferredWidth: 52; text: diskModel.formatCount(modelData.files); horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: "#56657b"; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }
            }

            // breadcrumb between the tables and the chart
            ColumnLayout {
                SplitView.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: "#ffffff"
                    border.color: "#dce2e8"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 4
                        ToolButton {
                            text: "←"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoBack
                            onClicked: diskModel.goBack()
                            ToolTip.visible: hovered; ToolTip.text: "后退 (Alt+←)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
                        }
                        ToolButton {
                            text: "→"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoForward
                            onClicked: diskModel.goForward()
                            ToolTip.visible: hovered; ToolTip.text: "前进 (Alt+→)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
                        }
                        ToolButton {
                            text: "↑"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoUp
                            onClicked: diskModel.goUp()
                            ToolTip.visible: hovered; ToolTip.text: "上一级 (←/退格)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
                        }
                        Row {
                            id: crumbRow
                            Layout.leftMargin: 6
                            spacing: 2
                            Repeater {
                                model: window.crumbs
                                delegate: Row {
                                    spacing: 2
                                    Label {
                                        text: modelData.name
                                        font.pixelSize: 13
                                        leftPadding: 3; rightPadding: 3
                                        color: modelData.current ? "#202b3c" : "#075db8"
                                        font.weight: modelData.current ? Font.DemiBold : Font.Normal
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: modelData.id.length > 0 && !modelData.current
                                            onClicked: diskModel.navigate(modelData.id)
                                        }
                                    }
                                    Label { text: "›"; font.pixelSize: 14; color: "#8a97a8"; visible: index < window.crumbs.length - 1 }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "当前目录 · " + diskModel.totalText + " · " + diskModel.formatCount(diskModel.nodeItems) + " 项"
                            color: "#66758a"; font.pixelSize: 11
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        color: "#ffffff"
                        border.color: "#dce2e8"
                        radius: 4
                        clip: true
                        ChartItem {
                            id: chart
                            objectName: "diskViewChart"
                            anchors.fill: parent
                            anchors.margins: 8
                            treeData: diskModel.chartTree
                            selectedId: diskModel.selectedId
                            viewMode: diskViewMode
                            centerLabel: diskModel.currentName
                            highlightExtension: window.highlightExt
                            onClicked: function(nodeId) { if (nodeId.length) diskModel.select(nodeId) }
                            onActivated: function(nodeId, directory) { diskModel.select(nodeId); if(directory) diskModel.navigate(nodeId) }
                            onContextRequested: function(nodeId, x, y) {
                                if (!nodeId.length) return
                                const info = chart.hoveredInfo(nodeId)
                                contextMenu.targetId = nodeId
                                contextMenu.targetPath = info.path || ""
                                contextMenu.parent = chart
                                contextMenu.x = x
                                contextMenu.y = y
                                contextMenu.open()
                            }
                            onCenterActivated: diskModel.goUp()
                        }
                        Rectangle {
                            id: hoverCard
                            z: 5
                            readonly property var info: chart.hoverId.length ? chart.hoveredInfo(chart.hoverId) : ({})
                            visible: chart.hoverId.length > 0
                            width: Math.min(320, parent.width - 16)
                            height: tip.implicitHeight + 16
                            x: Math.max(8, Math.min(parent.width - width - 8, chart.hoverPosition.x + 16))
                            y: Math.max(8, Math.min(parent.height - height - 8, chart.hoverPosition.y + 18))
                            color: "#fbfcfd"; border.color: "#c3ced9"; radius: 4
                            Column {
                                id: tip
                                anchors.fill: parent; anchors.margins: 8; spacing: 3
                                Label { width: parent.width; text: hoverCard.info.name || ""; font.weight: Font.DemiBold; font.pixelSize: 12; color: "#202b3c"; elide: Text.ElideRight }
                                Label { width: parent.width; text: hoverCard.info.path || ""; font.pixelSize: 11; color: "#66758a"; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                                Label {
                                    width: parent.width; font.pixelSize: 11; color: "#33445c"
                                    text: (hoverCard.info.directory ? "文件夹" : "文件") + " · "
                                          + diskModel.formatSize(hoverCard.info.size || 0) + " · "
                                          + (hoverCard.info.percent || 0).toFixed(1) + "%"
                                }
                                Label { width: parent.width; text: "单击固定详情 · 双击进入文件夹"; font.pixelSize: 10; color: "#8b98a9" }
                            }
                        }
                        Label {
                            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
                            text: "滚轮缩放 · 拖动平移 · 双击目录进入"
                            color: "#66758a"; font.pixelSize: 11
                        }
                    }
                }
            }
        }

        // ---- status bar --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 26; color: "#ffffff"; border.color: "#dce2e8"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                Label {
                    text: diskModel.selectedId.length ? "已选：" + (chart.hoveredInfo(diskModel.selectedId).name || "")
                                                      : "未选中"
                    color: "#56657b"; font.pixelSize: 11
                }
                Label {
                    Layout.fillWidth: true
                    text: scanService.state === "error" ? "扫描失败：" + scanService.errorText
                          : scanService.paused ? "扫描已暂停 · 点「继续」让它接着扫"
                          : diskModel.partial ? "部分结果 · 扫描未完成，数据仍在变化"
                          : scanService.skippedDirs > 0 ? "扫描完成 · " + scanService.skippedDirs + " 个目录无法读取"
                          : (scanService.fast ? "快扫" : "普通遍历") + " · " + scanService.statusText
                    color: scanService.state === "error" ? "#b3261e"
                          : scanService.paused || diskModel.partial || scanService.skippedDirs > 0 ? "#a76414" : "#66758a"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Label {
                    text: chart.hoverId.length ? (chart.hoveredInfo(chart.hoverId).path || "") : ""
                    color: "#8b98a9"; font.pixelSize: 11
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: parent.width * 0.5
                }
            }
        }
    }

    QtObject {
        id: columns
        readonly property int pct: 78
        readonly property int size: 84
        readonly property int alloc: 84
        readonly property int items: 62
        readonly property int files: 62
        readonly property int dirs: 58
        readonly property int modified: 118
        readonly property int attrs: 64
    }
}
