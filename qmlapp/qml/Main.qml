import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DiskViewPrototype

ApplicationWindow {
    id: window
    width: 1260
    height: 780
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: "Disk View · QML 技术样机"
    color: "#f4f6f8"

    DiskViewModel { id: diskModel; objectName: "diskModel" }
    Component.onCompleted: {
        diskModel.loadFixture(fixtureSource)
    }

    // The breadcrumb of the current folder, shortened when it gets long the
    // way the native panel shortens it.
    property var crumbs: {
        const all = diskModel.breadcrumb
        if (all.length <= 6) return all
        return [all[0], { name: "…", id: "", current: false }].concat(all.slice(all.length - 4))
    }

    Connections {
        target: scanService
        function onStarted(path) { diskModel.clear() }
        function onPartialTree(tree) { diskModel.loadTree(tree, true) }
        function onFinished(tree) { diskModel.loadTree(tree, false) }
        function onCancelled() { diskModel.setPartial(true) }
        function onFailed(message) { diskModel.setPartial(true) }
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
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
                BusyIndicator { running: scanService.state === "scanning"; visible: running; implicitWidth: 16; implicitHeight: 16 }
            }
        }

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

        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.leftMargin: 12; Layout.rightMargin: 12
            spacing: 4
            ToolButton {
                text: "←"; flat: true; implicitWidth: 28; implicitHeight: 28
                enabled: diskModel.canGoBack
                onClicked: diskModel.goBack()
                ToolTip.visible: hovered; ToolTip.text: "后退 (Alt+←)"
                contentItem: Label { text: parent.text; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
            }
            ToolButton {
                text: "→"; flat: true; implicitWidth: 28; implicitHeight: 28
                enabled: diskModel.canGoForward
                onClicked: diskModel.goForward()
                ToolTip.visible: hovered; ToolTip.text: "前进 (Alt+→)"
                contentItem: Label { text: parent.text; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
            }
            ToolButton {
                text: "↑"; flat: true; implicitWidth: 28; implicitHeight: 28
                enabled: diskModel.canGoUp
                onClicked: diskModel.goUp()
                ToolTip.visible: hovered; ToolTip.text: "上一级 (←/退格)"
                contentItem: Label { text: parent.text; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: "#33445c" }
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
                        Label {
                            text: "›"
                            font.pixelSize: 15
                            color: "#8a97a8"
                            visible: index < window.crumbs.length - 1
                            topPadding: 0
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label { text: diskModel.totalText; color: "#56657b" }
            Button { text: "1 层"; checkable: true; checked: chart.depth === 1; onClicked: chart.depth = 1 }
            Button { text: "2 层"; checkable: true; checked: chart.depth === 2; onClicked: chart.depth = 2 }
            Button { text: chart.viewMode === 0 ? "方块" : "圆盘"; onClicked: chart.viewMode = 1-chart.viewMode }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 12; spacing: 10
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 420
                color: "#ffffff"; border.color: "#dce2e8"; radius: 4; clip: true
                ChartItem {
                    id: chart; objectName: "diskViewChart"; anchors.fill: parent; anchors.margins: 8
                    treeData: diskModel.chartTree; selectedId: diskModel.selectedId; viewMode: diskViewMode; centerLabel: diskModel.currentName
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
                // The hover card: the same things the native tooltip shows.
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
                Label { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12; text: "滚轮缩放 · 拖动平移 · 双击目录进入"; color: "#66758a"; font.pixelSize: 11 }
            }
            Rectangle {
                Layout.preferredWidth: 420; Layout.fillHeight: true
                color: "#ffffff"; border.color: "#dce2e8"; radius: 4; clip: true
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 38; Layout.leftMargin: 10; Layout.rightMargin: 10
                        spacing: 6
                        Button {
                            text: "当前层"
                            flat: true
                            onClicked: diskModel.listMode = 0
                            background: Rectangle {
                                radius: 3
                                color: diskModel.listMode === 0 ? "#e3eefb" : "transparent"
                                border.color: "#dce2e8"
                            }
                        }
                        Button {
                            text: "最大文件"
                            flat: true
                            onClicked: diskModel.listMode = 1
                            background: Rectangle {
                                radius: 3
                                color: diskModel.listMode === 1 ? "#e3eefb" : "transparent"
                                border.color: "#dce2e8"
                            }
                        }
                        TextField {
                            id: filterField
                            Layout.fillWidth: true
                            Layout.maximumWidth: 190
                            placeholderText: "筛选列表，图表保持完整"
                            selectByMouse: true
                            onTextEdited: diskModel.filter = text
                        }
                        Label { text: diskModel.formatCount(diskModel.itemCount)+" 项"; color: "#66758a"; font.pixelSize: 12 }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#e5eaf0" }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 28; Layout.leftMargin: 12; Layout.rightMargin: 12
                        Label {
                            Layout.fillWidth: true
                            text: "名称" + (diskModel.sortKey === 1 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                            color: "#66758a"; font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: diskModel.toggleSort(1)
                            }
                        }
                        Label {
                            text: "大小" + (diskModel.sortKey === 0 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                            color: "#66758a"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 92
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: diskModel.toggleSort(0)
                            }
                        }
                        Label { text: "%"; color: "#66758a"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 48 }
                    }
                    ListView {
                        id: list; Layout.fillWidth: true; Layout.fillHeight: true
                        model: diskModel; clip: true; reuseItems: true; boundsBehavior: Flickable.StopAtBounds
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
                            required property string path
                            required property real percent
                            required property bool directory
                            width: ListView.view.width; height: 30
                            highlighted: nodeId === diskModel.selectedId
                            onClicked: diskModel.select(nodeId)
                            onDoubleClicked: if(directory) diskModel.navigate(nodeId)
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
                                spacing: 8
                                Label { text: directory ? "▸" : "▪"; color: directory ? "#168c84" : "#8190a3" }
                                Label { Layout.fillWidth: true; text: name; elide: Text.ElideMiddle; color: "#243246" }
                                Label { Layout.preferredWidth: 92; horizontalAlignment: Text.AlignRight; text: sizeText; color: "#56657b"; font.pixelSize: 12 }
                                Label { Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight; text: percent.toFixed(1)+"%"; color: "#56657b"; font.pixelSize: 12 }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#e5eaf0" }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        Layout.leftMargin: 12
                        Layout.rightMargin: 8
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            text: scanService.state === "error" ? "扫描失败：" + scanService.errorText
                                  : scanService.paused ? "扫描已暂停 · 点「继续」让它接着扫"
                                  : diskModel.partial ? "部分结果 · 扫描未完成，数据仍在变化"
                                  : scanService.skippedDirs > 0 ? "扫描完成 · " + scanService.skippedDirs + " 个目录无法读取"
                                  : "C++ 模型 · ListView 虚拟化 · 真实扫描已接入"
                            font.pixelSize: 11
                            color: scanService.state === "error" ? "#b3261e"
                                  : scanService.paused || diskModel.partial || scanService.skippedDirs > 0 ? "#a76414" : "#66758a"
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "上一页"
                            enabled: list.contentY > 0
                            onClicked: list.contentY = Math.max(0, list.contentY - list.height)
                        }
                        Button {
                            text: "下一页"
                            enabled: list.contentHeight - list.height - list.contentY > 1
                            onClicked: list.contentY = Math.min(list.contentHeight - list.height, list.contentY + list.height)
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 34; color: "#ffffff"; border.color: "#dce2e8"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                Label { text: diskModel.currentName; font.weight: Font.DemiBold; color: "#202b3c" }
                Label { Layout.fillWidth: true; text: diskModel.currentPath; elide: Text.ElideMiddle; color: "#56657b"; font.pixelSize: 12 }
                Label { text: scanService.fast ? "快扫" : "普通遍历"; color: "#56657b"; font.pixelSize: 12 }
                Label { text: scanService.statusText; color: "#56657b"; font.pixelSize: 12 }
                Label { text: "选中："+(diskModel.selectedId.length?diskModel.selectedId:"无"); color: "#56657b"; font.pixelSize: 12 }
            }
        }
    }
}
