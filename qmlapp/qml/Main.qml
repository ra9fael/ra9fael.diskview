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
                TextField {
                    id: pathField
                    Layout.preferredWidth: 260
                    Layout.maximumWidth: 300
                    text: initialScanPath
                    placeholderText: "扫描路径，例如 D:\\"
                    selectByMouse: true
                }
                Button {
                    text: (scanService.state === "done" || scanService.state === "cancelled") ? "重新扫描" : "扫描"
                    Layout.minimumWidth: 64
                    enabled: pathField.text.trim().length > 0 && scanService.state !== "scanning"
                    onClicked: scanService.start(pathField.text.trim())
                }
                Button {
                    text: scanService.paused ? "继续" : "暂停"
                    Layout.minimumWidth: 64
                    visible: scanService.state === "scanning"
                    enabled: scanService.state === "scanning"
                    onClicked: scanService.paused ? scanService.resume() : scanService.pause()
                }
                Button { text: "取消"; Layout.minimumWidth: 64; visible: scanService.state === "scanning"; enabled: scanService.state === "scanning"; onClicked: scanService.cancel() }
                Button {
                    text: "扫描问题"
                    Layout.minimumWidth: 76
                    visible: scanService.skippedDirs > 0 || scanService.skippedLinks > 0
                          || scanService.unknownSizes > 0 || scanService.fallbackReason.length > 0
                    onClicked: issuesDialog.open()
                }
                Button { text: "扫描设置"; Layout.minimumWidth: 76; onClicked: settingsDialog.open() }
                BusyIndicator { running: scanService.state === "scanning"; visible: running; implicitWidth: 16; implicitHeight: 16 }
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.leftMargin: 12; Layout.rightMargin: 12
            spacing: 8
            Button { text: "←"; enabled: diskModel.currentPath.indexOf("\\") >= 0; onClicked: diskModel.goUp() }
            Label { Layout.fillWidth: true; text: diskModel.breadcrumbText; elide: Text.ElideMiddle; color: "#075db8" }
            Label { text: diskModel.totalText; color: "#56657b" }
            Button { text: "1 层"; checkable: true; checked: true }
            Button { text: "2 层"; checkable: true }
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
                    onActivated: function(nodeId, directory) { diskModel.select(nodeId); if(directory) diskModel.navigate(nodeId) }
                    onHovered: function(nodeId) { if(nodeId.length) diskModel.select(nodeId) }
                    onCenterActivated: diskModel.goUp()
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
                        Label { text: "当前层"; font.weight: Font.DemiBold; color: "#202b3c" }
                        Item { Layout.fillWidth: true }
                        Label { text: diskModel.itemCount+" 项"; color: "#66758a"; font.pixelSize: 12 }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#e5eaf0" }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 28; Layout.leftMargin: 12; Layout.rightMargin: 12
                        Label { Layout.fillWidth: true; text: "名称"; color: "#66758a"; font.pixelSize: 11 }
                        Label { text: "大小"; color: "#66758a"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 92 }
                        Label { text: "%"; color: "#66758a"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 48 }
                    }
                    ListView {
                        id: list; Layout.fillWidth: true; Layout.fillHeight: true
                        model: diskModel; clip: true; reuseItems: true; boundsBehavior: Flickable.StopAtBounds
                        delegate: ItemDelegate {
                            required property string nodeId
                            required property string name
                            required property string sizeText
                            required property real percent
                            required property bool directory
                            width: ListView.view.width; height: 30
                            highlighted: nodeId === diskModel.selectedId
                            onClicked: diskModel.select(nodeId)
                            onDoubleClicked: if(directory) diskModel.navigate(nodeId)
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
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        leftPadding: 12
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
