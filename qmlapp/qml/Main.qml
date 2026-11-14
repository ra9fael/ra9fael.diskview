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
    // The whole window follows the host's light/dark choice; the chart reads
    // the same switch so its greys and inks follow.
    color: theme.window
    readonly property var themeTokens: theme

    DiskViewModel { id: diskModel; objectName: "diskModel" }
    // The file panel is its own model over the shared tree: its rows are the
    // files, its own whole-tree switch. The outline stays on the shared
    // model, whose rows are the folders (and their files, opened).
    FileListModel { id: fileListModel; model: diskModel }
    DockState { id: dockState; objectName: "diskViewDockState" }
    Component.onCompleted: {
        diskModel.loadFixture(fixtureSource)
        // The file view remembers how it was left.
        diskModel.fuzzyMatch = scanSettings.fuzzyMatch
        fileListModel.wholeTree = scanSettings.wholeTree
        // ... and so does the chart: how many levels of blocks it draws.
        diskModel.chartDepth = scanSettings.chartDepth
        chart.depth = scanSettings.chartDepth
        window.chartLayout = scanSettings.chartLayout
        try { dockState.layout = JSON.parse(scanSettings.dockLayout) } catch (e) {}
        verticalSplit.applyLayoutOrder()
        if (window.chartLayout === "dock") Qt.callLater(dockWorkspace.refresh)
    }

    // Which table columns are on. The name column is not listed: it never
    // goes away.
    function columnHidden(key) { return scanSettings.hiddenColumns.indexOf(key) >= 0 }
    function treeColumnHidden(key) {
        const minimum = {pct: 320, alloc: 440, items: 540, files: 610, dirs: 680, modified: 810, attributes: 900}
        return columnHidden(key) || treePane.width < (minimum[key] || 0)
    }
    function toggleColumn(key) {
        const hidden = scanSettings.hiddenColumns.slice()
        const at = hidden.indexOf(key)
        if (at >= 0) hidden.splice(at, 1)
        else hidden.push(key)
        scanSettings.hiddenColumns = hidden
        scanSettings.save()
    }
    function resizeColumn(which, key, delta) {
        const box = which === "file" ? fileColumns : columns
        const minimum = key === "pct" ? 48 : key === "modified" ? 80 : key === "attrs" ? 42 : 52
        box[key] = Math.max(minimum, box[key] + delta)
    }

    // The breadcrumb of the current folder, shortened when it gets long the
    // way the native panel shortens it.
    property var crumbs: {
        const all = diskModel.breadcrumb
        if (all.length <= 6) return all
        return [all[0], { name: "…", id: "", current: false }].concat(all.slice(all.length - 4))
    }
    // The extension the legend picked lives in the model now, so the file
    // list can filter on it too; the chart maps the "(无)" pick to its own
    // sentinel.
    function chartHighlight() {
        if (!diskModel.hasExtensionFilter) return ""
        return diskModel.extensionFilter.length ? diskModel.extensionFilter : "(none)"
    }
    // Which picture the chart draws: 0 treemap, 1 sunburst. The startup value
    // is the host's default view; the toolbar switches it for this window.
    property int chartKind: diskViewMode
    // The two panels' arrangement: "tablesTop" (default), "tablesBottom",
    // "tablesLeft" or "tablesRight". The tables keep their tree|legend split,
    // turned to match (side by side above/below, stacked left/right).
    property string chartLayout: "tablesTop"
    // "" or the pane that fills the window ("tables" / "chart").
    property string maximizedPane: ""
    property var dockPanes: ({tree: treePane, files: filePane, legend: legendPane, chart: chartPane})
    function detachForDock() {
        for (const pair of [[topSplit, treePane], [tablesInnerSplit, filePane],
                            [tablesInnerSplit, legendPane], [verticalSplit, chartPane]]) {
            for (let i = pair[0].count - 1; i >= 0; --i) {
                if (pair[0].itemAt(i) === pair[1]) pair[0].takeItem(i)
            }
        }
    }
    // Invoked by the standalone interaction checks. These are the very same
    // state operations as the drag/drop and divider handlers use.
    function verifyDock() {
        const saved = dockState.layout
        const oldLayout = chartLayout
        const ok = function() {
            return chartPane.parent !== null && chartPane.width > 0 && chartPane.height > 0
        }
        chartLayout = "dock"
        detachForDock()
        dockWorkspace.refresh()
        let passed = ok()
        passed = dockState.movePane("files", "tree", "center") && passed
        dockState.activate("files")
        passed = filePane.visible && !treePane.visible && passed
        passed = dockState.movePane("files", "chart", "bottom") && passed
        passed = dockState.setRatio("", .55) && passed
        for (const id of ["tree", "files", "legend", "chart"]) {
            maximizePane(id)
            passed = dockPanes[id].parent === fullscreenSlot && passed
            maximizePane(id)
            passed = dockPanes[id].parent !== fullscreenSlot && passed
        }
        dockState.layout = saved
        chartLayout = oldLayout
        if (oldLayout !== "dock") { restorePresetParents(); verticalSplit.applyLayoutOrder() }
        else dockWorkspace.refresh()
        return passed
    }
    function flushDockLayout() {
        scanSettings.dockLayout = JSON.stringify(dockState.layout)
        return scanSettings.save()
    }
    function restorePresetParents() {
        for (const id of ["tree", "files", "legend", "chart"]) {
            const pane = dockPanes[id]
            pane.anchors.fill = null
            pane.visible = true
            pane.parent = null
            if (id === "tree") topSplit.insertItem(0, pane)
            else if (id === "files") tablesInnerSplit.insertItem(0, pane)
            else if (id === "legend") tablesInnerSplit.addItem(pane)
            else verticalSplit.addItem(pane)
        }
    }
    function layoutVertical() { return chartLayout === "tablesTop" || chartLayout === "tablesBottom" }
    function layoutTablesFirst() { return chartLayout === "tablesTop" || chartLayout === "tablesLeft" }
    // Fullscreen: the named group is lifted into a layer of its own, over the
    // split, so no other panel can cover it. The pane is reparented into the
    // layer (it can only have one parent) and put back where the layout wants
    // it on the way out. "tables" is the whole tree|files|legend split, so a
    // table fullscreen is one move, not three.
    function maximizePane(group) {
        if (chartLayout === "dock") {
            if (group === "tables") group = "tree"
            const old = dockPanes[maximizedPane]
            if (old) old.anchors.fill = null
            maximizedPane = maximizedPane === group ? "" : group
            dockWorkspace.refresh()
            const pane = dockPanes[maximizedPane]
            if (pane) {
                pane.parent = fullscreenSlot
                pane.visible = true
                pane.anchors.fill = fullscreenSlot
            }
            return
        }
        const current = maximizedPane === "chart" ? chartPane : maximizedPane === "tables" ? topSplit : null
        const target = group === "chart" ? chartPane : group === "tables" ? topSplit : null
        // The same group again: back to the split.
        if (maximizedPane === group) {
            maximizedPane = ""
            restorePanes()
            return
        }
        // A different group first goes home, so the layer holds one pane.
        if (current && current !== target) {
            current.anchors.fill = null
            current.parent = null
            verticalSplit.addItem(current)
            verticalSplit.applyLayoutOrder()
        }
        if (!target) {
            maximizedPane = ""
            return
        }
        maximizedPane = group
        // The slot is the layer's only child; the pane fills it.
        target.parent = fullscreenSlot
        target.anchors.fill = fullscreenSlot
    }
    // Put the maximized pane back into the split, in the order the layout
    // asks for, and let the layout set the size budgets again.
    function restorePanes() {
        if (chartLayout === "dock") { dockWorkspace.refresh(); return }
        for (const pane of [topSplit, chartPane]) {
            if (pane.parent === fullscreenSlot) {
                pane.anchors.fill = null
                pane.parent = null
                verticalSplit.addItem(pane)
            }
        }
        verticalSplit.applyLayoutOrder()
    }
    // The host's look, through the theme tokens: one object holds every
    // colour, so the window, the chart and the dialogs follow the host's
    // light/dark choice and the system accent together.
    property bool dark: theme.dark

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
    Shortcut { sequence: "Esc"; onActivated: {
        // Out of the fullscreen first, then the text filter.
        if (window.maximizedPane) { window.maximizePane(window.maximizedPane); return }
        diskModel.filter = ""; filterField.text = ""
    } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- title row -------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: theme.surface
            border.color: theme.border
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 10
                Label { text: "Disk View"; font.pixelSize: 14; font.weight: Font.DemiBold; color: theme.text }
                Label { text: "QML 技术样机"; font.pixelSize: 12; color: theme.textMuted }
                Item { Layout.fillWidth: true }
                Label {
                    id: elevationLabel
                    text: scanService.state === "done" ? "真实扫描"
                          : scanService.state === "scanning" ? (scanService.paused ? "扫描已暂停" : "扫描中")
                          : scanService.state === "cancelled" ? "已取消的扫描"
                          : scanService.state === "error" ? "扫描失败"
                          : "固定快照"
                    font.pixelSize: 11
                    color: scanService.state === "done" ? "#168c84"
                          : scanService.state === "scanning" ? theme.warn
                          : scanService.state === "error" ? theme.error
                          : theme.textMuted
                }
                BusyIndicator { running: scanService.state === "scanning"; visible: running; implicitWidth: 14; implicitHeight: 14 }
            }
        }

        // ---- drive, path and scan controls -----------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: theme.surface
            border.color: theme.border
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
                // Whether the fast scan can run at all: this process carries an
                // administrator token only when an elevated Win顺 started it.
                Label {
                    visible: scanSettings.scanMode !== 2
                    text: scanService.elevated ? "快扫可用（管理员）" : "快扫不可用（Win顺 未以管理员运行）"
                    font.pixelSize: 11
                    color: scanService.elevated ? theme.ok : theme.warn
                    ToolTip.visible: elevationHover.hovered
                    ToolTip.text: scanService.elevated
                                  ? "本进程持有管理员令牌，快扫直接读取 MFT，不再弹出 UAC"
                                  : "以管理员身份运行 Win顺，并在插件权限里允许 elevated 后，快扫才会启用"
                    HoverHandler { id: elevationHover }
                }
            }
        }

        // ---- search scope and the columns menu ---------------------------
        // The tree panel and the file panel are separate now; the switches
        // that belonged to one of them moved onto the file panel's header.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: theme.surface
            border.color: theme.border
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 6
                Label {
                    text: "搜索："
                    font.pixelSize: 11; color: theme.textMuted
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    id: columnsButton
                    implicitWidth: 30
                    implicitHeight: 26
                    ToolTip.visible: hovered
                    ToolTip.text: "显示或隐藏列"
                    onClicked: columnsMenu.open()
                    background: Rectangle {
                        radius: 3
                        color: columnsButton.hovered ? theme.hoverRow : "transparent"
                        border.color: theme.border
                    }
                    contentItem: Glyph { iconName: "list-numbers"; size: 15; color: theme.textMuted }
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
                    placeholderText: "筛选名称或文件…"
                    selectByMouse: true
                    onTextEdited: diskModel.filter = text
                }
            }
        }

        // ---- tables and the chart, in the order the user picked ----------
        // A plain Item is the dock workspace: the split fills it, and the
        // fullscreen layer draws over the split when a pane is maximized, so
        // a maximized panel is never covered by another one.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

        SplitView {
            id: verticalSplit
            visible: window.chartLayout !== "dock"
            orientation: Qt.Vertical
            anchors.fill: parent
            handle: Rectangle { implicitHeight: 5; color: theme.handleFill; border.color: theme.border }

            // A SplitView's pane order is the order its children were added in.
            // Both panes are real items with stable ids; applyLayoutOrder() puts
            // them back in the chosen order and orientation, which is the whole
            // layout switch.
            function applyLayoutOrder() {
                if (window.chartLayout === "dock") {
                    window.detachForDock()
                    Qt.callLater(dockWorkspace.refresh)
                    return
                }
                const vertical = window.layoutVertical()
                // The outer split turns with the layout; the tables' inner one
                // carries the three panels.
                const first = window.layoutTablesFirst() ? topSplit : chartPane
                const second = window.layoutTablesFirst() ? chartPane : topSplit
                verticalSplit.orientation = vertical ? Qt.Vertical : Qt.Horizontal
                for (const pane of [first, second]) {
                    pane.parent = null
                    verticalSplit.addItem(pane)
                }
                // A pane in a vertical split needs a height budget, in a
                // horizontal one a width budget; anything else collapses it.
                if (vertical) {
                    // tables above/below the chart: the tree | the files over
                    // the legend
                    topSplit.orientation = Qt.Horizontal
                    topSplit.SplitView.preferredHeight = 320
                    topSplit.SplitView.preferredWidth = -1
                    treePane.SplitView.fillWidth = true
                    treePane.SplitView.preferredWidth = -1
                    treePane.SplitView.minimumWidth = 430
                    treePane.SplitView.minimumHeight = -1
                    tablesInnerSplit.SplitView.fillWidth = false
                    tablesInnerSplit.SplitView.preferredWidth = 380
                    tablesInnerSplit.SplitView.minimumWidth = 280
                    tablesInnerSplit.orientation = Qt.Vertical
                    filePane.SplitView.fillHeight = true
                    filePane.SplitView.preferredHeight = -1
                    filePane.SplitView.minimumHeight = 120
                    filePane.SplitView.minimumWidth = -1
                    legendPane.SplitView.fillHeight = false
                    legendPane.SplitView.preferredHeight = 190
                    legendPane.SplitView.minimumHeight = 80
                    legendPane.SplitView.minimumWidth = -1
                } else {
                    // tables beside the chart: the three panels side by side
                    topSplit.orientation = Qt.Vertical
                    topSplit.SplitView.preferredWidth = 640
                    topSplit.SplitView.preferredHeight = -1
                    tablesInnerSplit.orientation = Qt.Horizontal
                    treePane.SplitView.fillWidth = true
                    treePane.SplitView.preferredWidth = -1
                    treePane.SplitView.minimumWidth = 300
                    treePane.SplitView.minimumHeight = -1
                    filePane.SplitView.fillHeight = false
                    filePane.SplitView.preferredHeight = -1
                    filePane.SplitView.minimumHeight = -1
                    filePane.SplitView.fillWidth = false
                    filePane.SplitView.preferredWidth = 300
                    filePane.SplitView.minimumWidth = 220
                    legendPane.SplitView.fillWidth = false
                    legendPane.SplitView.preferredWidth = 280
                    legendPane.SplitView.minimumWidth = 220
                    legendPane.SplitView.minimumHeight = -1
                }
                // The fullscreen layer holds a maximized pane now; the split
                // keeps its own children, so nothing is hidden here.
            }

            // folder table | extension legend — a third panel, the file list,
            // joins them when the dock layout gives it room. The tree keeps
            // its own width, the file list opens beside it.
            SplitView {
                id: topSplit
                orientation: Qt.Horizontal
                SplitView.preferredHeight: 320
                SplitView.minimumHeight: 110
                handle: Rectangle { implicitWidth: 5; color: theme.handleFill; border.color: theme.border }

                Item {
                    id: treePane
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 430
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.surfaceAlt
                            border.color: theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 120
                                    text: "文件夹 / 名称" + (diskModel.sortKey === 1 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted
                                    elide: Text.ElideRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(1) }
                                }
                                Item {
                                    Layout.preferredWidth: 12
                                    Layout.fillHeight: true
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeHorCursor
                                        property real startX
                                        onPressed: startX = mouse.x
                                        onPositionChanged: if (pressed) window.resizeColumn("tree", "pct", mouse.x - startX)
                                    }
                                }
                                Label {
                                    visible: !window.columnHidden("pct")
                                    Layout.preferredWidth: columns.pct
                                    text: "% 父级"
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    Layout.preferredWidth: columns.size
                                    text: "大小" + (diskModel.sortKey === 0 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted
                                    horizontalAlignment: Text.AlignRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(0) }
                                }
                                Label { visible: !window.treeColumnHidden("alloc"); Layout.preferredWidth: columns.alloc; text: "已分配"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.treeColumnHidden("items"); Layout.preferredWidth: columns.items; text: "项目"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.treeColumnHidden("files"); Layout.preferredWidth: columns.files; text: "文件"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { visible: !window.treeColumnHidden("dirs"); Layout.preferredWidth: columns.dirs; text: "文件夹"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label {
                                    visible: !window.treeColumnHidden("modified")
                                    Layout.preferredWidth: columns.modified
                                    text: "修改时间" + (diskModel.sortKey === 2 ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted
                                    horizontalAlignment: Text.AlignRight
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: diskModel.toggleSort(2) }
                                }
                                Label { visible: !window.treeColumnHidden("attributes"); Layout.preferredWidth: columns.attrs; text: "属性"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                            }
                        }
                        ListView {
                            id: list
                            objectName: "diskViewList"
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
                                // A click only selects; the triangle and the
                                // double click are the two ways to open a
                                // folder, so a single click never jumps.
                                onClicked: diskModel.select(nodeId)
                                // The list is an outline: a double click opens
                                // the folder in place. Drilling happens in the
                                // chart, which then opens the list down to it.
                                onDoubleClicked: if (directory && hasChildren) diskModel.toggleExpanded(nodeId)
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
                                    clip: true
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6 + depth * 12
                                            spacing: 4
                                                Label {
                                                    Layout.preferredWidth: 12
                                                    visible: rowItem.hasChildren
                                                    text: rowItem.expanded ? "▾" : "▸"
                                                    color: theme.textMuted; font.pixelSize: 10
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
                                                    color: theme.text; font.pixelSize: 11
                                                }
                                            }
                                    }
                                    Item {
                                        visible: !window.columnHidden("pct")
                                        Layout.preferredWidth: fileColumns.pct
                                        Layout.fillHeight: true
                                        Rectangle {
                                            x: 4
                                            y: parent.height / 2 - 4
                                            height: 8
                                            radius: 2
                                            color: theme.selection
                                            width: Math.max(0, Math.min(1, rowItem.percent / 100)) * (parent.width - 8)
                                        }
                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            verticalAlignment: Text.AlignVCenter
                                            text: rowItem.percent.toFixed(1) + "%"
                                            color: theme.text; font.pixelSize: 11
                                        }
                                    }
                                    Label { Layout.preferredWidth: columns.size; text: rowItem.sizeText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.text; font.pixelSize: 11 }
                                    Label { visible: !window.treeColumnHidden("alloc"); Layout.preferredWidth: columns.alloc; text: rowItem.allocText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                    Label {
                                        visible: !window.treeColumnHidden("items")
                                        Layout.preferredWidth: columns.items
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.items) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: theme.textMuted; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.treeColumnHidden("files")
                                        Layout.preferredWidth: columns.files
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.files) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: theme.textMuted; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.treeColumnHidden("dirs")
                                        Layout.preferredWidth: columns.dirs
                                        text: rowItem.directory ? diskModel.formatCount(rowItem.dirs) : "—"
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: theme.textMuted; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.treeColumnHidden("modified")
                                        Layout.preferredWidth: columns.modified
                                        text: rowItem.modifiedText
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: theme.textMuted; font.pixelSize: 11
                                    }
                                    Label {
                                        visible: !window.treeColumnHidden("attributes")
                                        Layout.preferredWidth: columns.attrs
                                        text: rowItem.attributes
                                        horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                        color: theme.textMuted; font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                // The three panels: the tree | the files above the legend.
                // The inner split keeps the files and the legend stacked so a
                // narrow window still leaves the tree its columns.
                SplitView {
                    id: tablesInnerSplit
                    orientation: Qt.Vertical
                    SplitView.fillWidth: false
                    SplitView.preferredWidth: 380
                    SplitView.minimumWidth: 280
                    handle: Rectangle { implicitWidth: 5; color: theme.handleFill; border.color: theme.border }

                    Item {
                        id: filePane
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 120
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.surfaceAlt
                            border.color: theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: "文件" + (fileListModel.wholeTree ? " · 整个扫描" : "")
                                    font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted
                                }
                                // Where the files come from: the folder the
                                // chart is on, or the whole scan.
                                ComboBox {
                                    implicitWidth: 78
                                    implicitHeight: 22
                                    font.pixelSize: 10
                                    model: ["当前目录", "整个扫描"]
                                    currentIndex: fileListModel.wholeTree ? 1 : 0
                                    onActivated: {
                                        fileListModel.wholeTree = (index === 1)
                                        scanSettings.wholeTree = fileListModel.wholeTree
                                        scanSettings.save()
                                    }
                                }
                                // How loosely a text matches: a substring of
                                // the path, or the fuzzy subsequence of the
                                // name.
                                ComboBox {
                                    implicitWidth: 78
                                    implicitHeight: 22
                                    font.pixelSize: 10
                                    model: ["子串匹配", "模糊匹配"]
                                    currentIndex: diskModel.fuzzyMatch ? 1 : 0
                                    onActivated: {
                                        diskModel.fuzzyMatch = (index === 1)
                                        scanSettings.fuzzyMatch = diskModel.fuzzyMatch
                                        scanSettings.save()
                                    }
                                }
                            }
                        }
                        ListView {
                            id: fileList
                            objectName: "diskViewFileList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: fileListModel
                            clip: true
                            reuseItems: true
                            boundsBehavior: Flickable.StopAtBounds
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Up) { fileListModel.moveSelection(-1); event.accepted = true }
                                else if (event.key === Qt.Key_Down) { fileListModel.moveSelection(1); event.accepted = true }
                                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { fileListModel.openSelected(); event.accepted = true }
                            }
                            delegate: ItemDelegate {
                                id: fileRow
                                required property string nodeId
                                required property string name
                                required property string sizeText
                                required property string allocText
                                required property string path
                                required property string extColor
                                required property string modifiedText
                                required property string attributes
                                required property real percent
                                required property bool selected
                                width: ListView.view.width
                                height: 24
                                padding: 0
                                highlighted: fileRow.selected || nodeId === diskModel.selectedId
                                onClicked: fileListModel.select(nodeId)
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onSingleTapped: function(eventPoint) {
                                        contextMenu.targetId = fileRow.nodeId
                                        contextMenu.targetPath = fileRow.path
                                        contextMenu.parent = fileRow
                                        contextMenu.x = eventPoint.position.x
                                        contextMenu.y = eventPoint.position.y
                                        contextMenu.open()
                                    }
                                }
                                contentItem: RowLayout {
                                    spacing: 0
                                    clip: true
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            spacing: 4
                                            Rectangle { Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 2; color: fileRow.extColor }
                                            Label {
                                                Layout.fillWidth: true
                                                text: fileRow.name
                                                elide: Text.ElideRight
                                                color: theme.text; font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Item {
                                        visible: !window.columnHidden("pct")
                                        Layout.preferredWidth: columns.pct
                                        Layout.fillHeight: true
                                        clip: true
                                        Rectangle {
                                            x: 4
                                            y: parent.height / 2 - 4
                                            height: 8
                                            radius: 2
                                            color: theme.selection
                                            width: Math.max(0, Math.min(1, fileRow.percent / 100)) * (parent.width - 8)
                                        }
                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            verticalAlignment: Text.AlignVCenter
                                            text: fileRow.percent.toFixed(1) + "%"
                                            color: theme.text; font.pixelSize: 11
                                        }
                                    }
                                    Label { Layout.preferredWidth: fileColumns.size; text: fileRow.sizeText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.text; font.pixelSize: 11 }
                                    Label { visible: !window.columnHidden("alloc") && filePane.width > 460; Layout.preferredWidth: fileColumns.alloc; text: fileRow.allocText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                    Label { visible: !window.columnHidden("modified") && filePane.width > 560; Layout.preferredWidth: fileColumns.modified; text: fileRow.modifiedText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                    Label { visible: !window.columnHidden("attributes") && filePane.width > 640; Layout.preferredWidth: fileColumns.attrs; text: fileRow.attributes; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: legendPane
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 250
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.surfaceAlt
                            border.color: theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                                Label { Layout.preferredWidth: 18; text: "" }
                                Label { Layout.fillWidth: true; Layout.minimumWidth: 70; text: "扩展名"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted }
                                Label { Layout.preferredWidth: 52; text: "占比"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 66; text: "大小"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 66; text: "已分配"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
                                Label { Layout.preferredWidth: 52; text: "文件数"; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.textMuted; horizontalAlignment: Text.AlignRight }
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
                                // Two links in one: the row the legend picked
                                // (which also filters the file list), and the
                                // extension of whatever the chart has
                                // selected, so the panels point at each other.
                                highlighted: (diskModel.hasExtensionFilter && diskModel.extensionFilter === modelData.ext)
                                             || (diskModel.selectedExtension.length > 0 && diskModel.selectedExtension === modelData.ext)
                                onClicked: diskModel.selectExtension(modelData.ext)
                                contentItem: RowLayout {
                                    spacing: 0
                                    clip: true
                                    Item { Layout.preferredWidth: 18; Layout.fillHeight: true
                                        Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 2; color: modelData.color }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 70
                                        text: (modelData.ext.length ? modelData.ext : "(无)") + "  " + modelData.label
                                        elide: Text.ElideRight
                                        color: theme.text; font.pixelSize: 11
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
                                            color: theme.text; font.pixelSize: 11
                                        }
                                    }
                                    Label { Layout.preferredWidth: 66; text: modelData.sizeText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.text; font.pixelSize: 11 }
                                    Label { Layout.preferredWidth: 66; text: modelData.allocText; horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                    Label { Layout.preferredWidth: 52; text: diskModel.formatCount(modelData.files); horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; color: theme.textMuted; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }
                } // tablesInnerSplit
            }

            // the chart pane: its toolbar (picture, depth, layout) and the
            // chart itself. The breadcrumb above it stays in this pane so the
            // two panels always describe the same folder.
            ColumnLayout {
                id: chartPane
                SplitView.fillHeight: true
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: theme.surface
                    border.color: theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 4
                        ToolButton {
                            text: "←"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoBack
                            onClicked: diskModel.goBack()
                            ToolTip.visible: hovered; ToolTip.text: "后退 (Alt+←)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: theme.text }
                        }
                        ToolButton {
                            text: "→"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoForward
                            onClicked: diskModel.goForward()
                            ToolTip.visible: hovered; ToolTip.text: "前进 (Alt+→)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: theme.text }
                        }
                        ToolButton {
                            text: "↑"; flat: true; implicitWidth: 26; implicitHeight: 26
                            enabled: diskModel.canGoUp
                            onClicked: diskModel.goUp()
                            ToolTip.visible: hovered; ToolTip.text: "上一级 (←/退格)"
                            contentItem: Label { text: parent.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; opacity: parent.enabled ? 1 : 0.35; color: theme.text }
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
                                        color: modelData.current ? theme.text : theme.accent
                                        font.weight: modelData.current ? Font.DemiBold : Font.Normal
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: modelData.id.length > 0 && !modelData.current
                                            onClicked: diskModel.navigate(modelData.id)
                                        }
                                    }
                                    Label { text: "›"; font.pixelSize: 14; color: theme.textMuted; visible: index < window.crumbs.length - 1 }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "当前目录 · " + diskModel.totalText + " · " + diskModel.formatCount(diskModel.nodeItems) + " 项"
                            color: theme.textMuted; font.pixelSize: 11
                        }
                    }
                }

                // The chart's own toolbar: which picture and how deep. These
                // belong to the chart, not to the scan, so they live here.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8; Layout.rightMargin: 8
                    spacing: 6
                    // The chart's view modes, as icons with their names in the
                    // tooltip: squares for the treemap, a ring for the sunburst.
                    ToolButton {
                        id: treemapButton
                        implicitWidth: 30
                        implicitHeight: 26
                        checked: window.chartKind === 0
                        ToolTip.visible: hovered
                        ToolTip.text: "方块（矩形树图）"
                        onClicked: window.chartKind = 0
                        background: Rectangle {
                            radius: 3
                            color: treemapButton.checked ? theme.selection : "transparent"
                            border.color: theme.border
                        }
                        contentItem: Glyph { iconName: "squares-four"; size: 15; color: treemapButton.checked ? theme.text : theme.textMuted }
                    }
                    ToolButton {
                        id: sunburstButton
                        implicitWidth: 30
                        implicitHeight: 26
                        checked: window.chartKind === 1
                        ToolTip.visible: hovered
                        ToolTip.text: "圆盘（旭日图）"
                        onClicked: window.chartKind = 1
                        background: Rectangle {
                            radius: 3
                            color: sunburstButton.checked ? theme.selection : "transparent"
                            border.color: theme.border
                        }
                        contentItem: Glyph { iconName: "chart-donut"; size: 15; color: sunburstButton.checked ? theme.text : theme.textMuted }
                    }
                    // Back to the folder the tree is rooted at and to 1:1: one
                    // button for both, since a chart that has wandered off is
                    // what brings the user here.
                    ToolButton {
                        id: resetButton
                        implicitWidth: 30
                        implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: "复位：回到当前目录并还原缩放"
                        onClicked: {
                            diskModel.resetChart()
                            chart.resetView()
                        }
                        background: Rectangle {
                            radius: 3
                            color: resetButton.hovered ? theme.hoverRow : "transparent"
                            border.color: theme.border
                        }
                        contentItem: Glyph { iconName: "arrow-counter-clockwise"; size: 15; color: theme.textMuted }
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: "层级"; font.pixelSize: 11; color: theme.textMuted }
                    ComboBox {
                        id: depthBox
                        implicitWidth: 76
                        implicitHeight: 26
                        font.pixelSize: 11
                        model: ["1", "2", "3", "4", "5"]
                        currentIndex: chart.depth - 1
                        ToolTip.visible: hovered
                        ToolTip.text: "方块里再画几层文件夹"
                        onActivated: {
                            chart.depth = index + 1
                            diskModel.chartDepth = index + 1
                            scanSettings.chartDepth = index + 1
                            scanSettings.save()
                        }
                    }
                    ToolButton {
                        visible: window.chartLayout === "dock"
                        implicitWidth: 28; implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: "恢复默认 Dock 布局"
                        onClicked: dockState.reset()
                        contentItem: Glyph { iconName: "layout"; size: 15; color: theme.textMuted }
                    }
                    Label { text: "布局"; font.pixelSize: 11; color: theme.textMuted }
                    ComboBox {
                        id: layoutBox
                        implicitWidth: 104
                        implicitHeight: 26
                        font.pixelSize: 11
                        model: ["表格在上", "图表在上", "表格在左", "图表在左", "自由 Dock"]
                        currentIndex: Math.max(0, ["tablesTop", "tablesBottom", "tablesLeft", "tablesRight", "dock"].indexOf(window.chartLayout))
                        ToolTip.visible: hovered
                        ToolTip.text: "表格与图表怎么排"
                        onActivated: {
                            const names = ["tablesTop", "tablesBottom", "tablesLeft", "tablesRight", "dock"]
                            if (window.maximizedPane) window.maximizePane(window.maximizedPane)
                            const wasDock = window.chartLayout === "dock"
                            window.chartLayout = names[index]
                            if (wasDock && window.chartLayout !== "dock") window.restorePresetParents()
                            scanSettings.chartLayout = window.chartLayout
                            scanSettings.save()
                            verticalSplit.applyLayoutOrder()
                        }
                    }
                    ToolButton {
                        id: tablesFullscreenButton
                        implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: window.maximizedPane === "tables" ? "还原表格" : "表格全屏"
                        onClicked: window.maximizePane("tables")
                        contentItem: Row {
                            spacing: 4
                            Glyph { iconName: window.maximizedPane === "tables" ? "arrows-in" : "arrows-out"; size: 14; color: theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                            Label {
                                text: window.maximizedPane === "tables" ? "还原" : "表格全屏"
                                font.pixelSize: 11
                                color: theme.textMuted
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                    ToolButton {
                        id: chartFullscreenButton
                        implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: window.maximizedPane === "chart" ? "还原图表" : "图表全屏"
                        onClicked: window.maximizePane("chart")
                        contentItem: Row {
                            spacing: 4
                            Glyph { iconName: window.maximizedPane === "chart" ? "arrows-in" : "arrows-out"; size: 14; color: theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                            Label {
                                text: window.maximizedPane === "chart" ? "还原" : "图表全屏"
                                font.pixelSize: 11
                                color: theme.textMuted
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        color: theme.surface
                        border.color: theme.border
                        radius: 4
                        clip: true
                        ChartItem {
                            id: chart
                            objectName: "diskViewChart"
                            anchors.fill: parent
                            anchors.margins: 8
                            treeData: diskModel.chartTree
                            selectedId: diskModel.selectedId
                            viewMode: window.chartKind
                            centerLabel: diskModel.currentName
                            highlightExtension: window.chartHighlight()
                            accent: window.themeTokens.accent
                            onClicked: function(nodeId) { if (nodeId.length) diskModel.select(nodeId) }
                            onActivated: function(nodeId, directory) {
                                // The chart is a map: a double click enters the
                                // folder, the same way the native one does. The
                                // tree keeps the in-place triangle for opening
                                // a folder without leaving the current one.
                                diskModel.enterNode(nodeId)
                            }
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
                            color: theme.hoverRow; border.color: "#c3ced9"; radius: 4
                            Column {
                                id: tip
                                anchors.fill: parent; anchors.margins: 8; spacing: 3
                                Label { width: parent.width; text: hoverCard.info.name || ""; font.weight: Font.DemiBold; font.pixelSize: 12; color: theme.text; elide: Text.ElideRight }
                                Label { width: parent.width; text: hoverCard.info.path || ""; font.pixelSize: 11; color: theme.textMuted; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                                Label {
                                    width: parent.width; font.pixelSize: 11; color: theme.text
                                    text: (hoverCard.info.directory ? "文件夹" : "文件") + " · "
                                          + diskModel.formatSize(hoverCard.info.size || 0) + " · "
                                          + (hoverCard.info.percent || 0).toFixed(1) + "%"
                                }
                                Label { width: parent.width; text: "单击固定详情 · 双击进入文件夹"; font.pixelSize: 10; color: theme.textMuted }
                            }
                        }
                        // The old hint label lives in the launcher's help
                        // preview now; the chart area stays clean.
                    }
                }
            }
        }

            DockWorkspace {
                id: dockWorkspace
                objectName: "diskViewDockWorkspace"
                anchors.fill: parent
                visible: window.chartLayout === "dock"
                panes: window.dockPanes
                state: dockState
                fullscreen: window.maximizedPane
                onMaximizeRequested: function(pane) { window.maximizePane(pane) }
                onPersisted: {
                    scanSettings.dockLayout = JSON.stringify(dockState.layout)
                    dockSave.restart()
                }
            }
            Timer { id: dockSave; interval: 300; onTriggered: scanSettings.save() }

            // The fullscreen layer: the maximized pane lives here, above the
            // split, so nothing behind it can cover it. It is a layer of its
            // own rather than a hidden sibling, which is what the old
            // visibility toggle got wrong.
            Item {
                id: fullscreenLayer
                anchors.fill: parent
                z: 10000
                visible: window.maximizedPane !== ""
                Rectangle {
                    anchors.fill: parent
                    color: theme.window
                    border.color: theme.border
                }
                Item { id: fullscreenSlot; anchors.fill: parent }
                ToolButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    z: 10001
                    implicitWidth: 32
                    implicitHeight: 32
                    ToolTip.visible: hovered
                    ToolTip.text: "还原 (Esc)"
                    onClicked: window.maximizePane(window.maximizedPane)
                    contentItem: Glyph { iconName: "arrows-in"; size: 16; color: theme.text }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? theme.hoverRow : theme.surface
                        border.color: theme.border
                    }
                }
            }
        } // the dock workspace

        // ---- status bar --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 26; color: theme.surface; border.color: theme.border
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                Label {
                    text: diskModel.selectedId.length ? "已选：" + (chart.hoveredInfo(diskModel.selectedId).name || "")
                                                      : "未选中"
                    color: theme.textMuted; font.pixelSize: 11
                }
                Label {
                    Layout.fillWidth: true
                    text: diskModel.loadError.length ? diskModel.loadError
                          : scanService.state === "error" ? "扫描失败：" + scanService.errorText
                          : scanService.paused ? "扫描已暂停 · 点「继续」让它接着扫"
                          : diskModel.partial ? "部分结果 · 扫描未完成，数据仍在变化"
                          : scanService.skippedDirs > 0 ? "扫描完成 · " + scanService.skippedDirs + " 个目录无法读取"
                           : (scanService.fast ? "MFT 快扫" : "普通遍历") + " · " + scanService.statusText
                    color: diskModel.loadError.length || scanService.state === "error" ? theme.error
                          : scanService.paused || diskModel.partial || scanService.skippedDirs > 0 ? theme.warn : theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Label {
                    text: chart.hoverId.length ? (chart.hoveredInfo(chart.hoverId).path || "") : ""
                    color: theme.textMuted; font.pixelSize: 11
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: parent.width * 0.5
                }
            }
        }
    }

    QtObject {
        id: columns
        property int pct: 78
        property int size: 84
        property int alloc: 84
        property int items: 62
        property int files: 62
        property int dirs: 58
        property int modified: 118
        property int attrs: 64
    }
    QtObject {
        id: fileColumns
        property int pct: 78
        property int size: 84
        property int alloc: 84
        property int modified: 118
        property int attrs: 64
    }
}
