import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "themeSettingsDialog"
    title: "主题设置"
    modal: true
    width: 430
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton
    // The buttons live inside the content, not in a custom `footer`: the
    // footer is docked to the popup's bottom edge without being part of its
    // implicit height, and the buttons ended up over (or off) the frame.

    onOpened: {
        fontBox.value = scanSettings.fontSize
        treeRowBox.value = scanSettings.treeRowHeight
        fileRowBox.value = scanSettings.fileRowHeight
        headerRowBox.value = scanSettings.headerRowHeight
        modeBox.currentIndex = scanSettings.appearanceMode
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: metrics.gap

        Label {
            Layout.fillWidth: true
            text: "颜色与圆角跟随 Win顺 当前主题；这里调整磁盘视图的信息密度。"
            color: theme.textMuted
            wrapMode: Text.WordWrap
            font.pixelSize: metrics.small
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: metrics.gap
            rowSpacing: metrics.gap

            Label { text: "基础字号"; color: theme.text }
            SpinBox {
                id: fontBox
                from: 11; to: 16; stepSize: 1
                editable: true
                Layout.fillWidth: true
            }
            Label { text: "颜色主题"; color: theme.text }
            ComboBox {
                id: modeBox
                model: ["跟随宿主", "浅色", "深色"]
                Layout.fillWidth: true
            }
            Label { text: "树视图行高"; color: theme.text }
            SpinBox {
                id: treeRowBox
                from: 22; to: 40; stepSize: 1
                editable: true
                Layout.fillWidth: true
            }
            Label { text: "文件视图行高"; color: theme.text }
            SpinBox {
                id: fileRowBox
                from: 22; to: 44; stepSize: 1
                editable: true
                Layout.fillWidth: true
            }
            Label { text: "表头行高"; color: theme.text }
            SpinBox {
                id: headerRowBox
                from: 22; to: 36; stepSize: 1
                editable: true
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: "可视化层级默认使用“自动”，可在可视化工具栏中单独调整。"
            color: theme.textMuted
            wrapMode: Text.WordWrap
            font.pixelSize: metrics.small
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: metrics.gap
            Item { Layout.fillWidth: true }
            ThemedButton { text: "取消"; onClicked: root.reject() }
            ThemedButton {
                text: "确定"
                onClicked: {
                    scanSettings.fontSize = fontBox.value
                    scanSettings.treeRowHeight = treeRowBox.value
                    scanSettings.fileRowHeight = fileRowBox.value
                    scanSettings.headerRowHeight = headerRowBox.value
                    scanSettings.appearanceMode = modeBox.currentIndex
                    scanSettings.save()
                    appHost.setLocalAppearanceMode(modeBox.currentIndex)
                    root.accept()
                }
            }
        }
    }
}
