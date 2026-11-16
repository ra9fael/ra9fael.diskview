import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What the scan could not read: the directories it was not allowed into, the
// links it did not follow and the files whose size it could not determine.
// A scan that lists them is still usable; the list says what is missing.
Dialog {
    id: root

    title: "扫描问题"
    modal: true
    width: 720
    height: 420
    anchors.centerIn: Overlay.overlay

    standardButtons: Dialog.NoButton
    footer: RowLayout {
        spacing: 8
        Item { Layout.fillWidth: true }
        Button { text: "关闭"; onClicked: root.close() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: "无法读取 " + scanService.skippedDirs + " 个目录"
                  + (scanService.skippedLinks > 0 ? " · 未跟随 " + scanService.skippedLinks + " 个链接" : "")
                  + (scanService.unknownSizes > 0 ? " · " + scanService.unknownSizes + " 个文件大小未知" : "")
                  + (scanService.orphanRecords > 0 ? " · " + scanService.orphanRecords + " 条孤立记录" : "")
            color: theme.text
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            visible: scanService.fallbackReason.length > 0
            text: "快扫没有生效：" + scanService.fallbackReason
            color: theme.warn
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.surface
            border.color: theme.border
            radius: 4

            ListView {
                id: issueList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                spacing: 2
                model: scanService.issues
                delegate: RowLayout {
                    width: issueList.width
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: modelData.path
                        elide: Text.ElideMiddle
                        color: theme.text
                        font.pixelSize: 12
                    }
                    Label {
                        Layout.preferredWidth: 260
                        text: modelData.message.length > 0 ? modelData.message : ("错误 " + modelData.code)
                        elide: Text.ElideRight
                        color: theme.error
                        font.pixelSize: 12
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: scanService.issues.length === 0
                text: "没有问题"
                color: theme.textMuted
                font.pixelSize: 12
            }
        }

        Label {
            Layout.fillWidth: true
            text: "扫描方式：" + (scanService.fast ? "快扫（MFT）" : "普通遍历")
            color: theme.textMuted
            font.pixelSize: 11
        }
    }
}
