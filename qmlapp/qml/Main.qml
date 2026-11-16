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
    title: "Disk View"
    font.family: metrics.fontFamily
    // The whole window follows the host's light/dark choice; the chart reads
    // the same switch so its greys and inks follow. On Mica the window itself
    // is see-through (theme.window) and the page behind the panes is the
    // host's background, which is translucent then.
    color: theme.window
    readonly property var themeTokens: theme
    readonly property var metrics: Metrics
    property string committedPath: initialScanPath
    property bool acceptingSuggestion: false
    property int suggestionRequestId: 0
    property string notice: ""
    property alias filterField: filePaneHeader.filterField
    // The Basic style's controls take their colours from the palette; feeding
    // it the same tokens keeps a TextField, Button and ComboBox from staying
    // light in a dark WinShun.
    palette.window: theme.backdrop ? theme.surface : theme.window
    palette.windowText: theme.text
    palette.base: theme.surface
    palette.alternateBase: theme.surfaceAlt
    palette.text: theme.text
    palette.button: theme.surfaceAlt
    palette.buttonText: theme.text
    palette.brightText: theme.error
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    palette.placeholderText: theme.textMuted
    palette.toolTipBase: theme.backdrop ? theme.surfaceAlt : theme.surface
    palette.toolTipText: theme.text
    palette.mid: theme.border
    palette.midlight: theme.border
    palette.dark: theme.border
    palette.light: theme.surface
    palette.shadow: theme.border

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
        fileListModel.fuzzyMatch = scanSettings.fuzzyMatch
        fileListModel.wholeTree = scanSettings.wholeTree
        fileListModel.recursive = scanSettings.fileRecursive
        // ... and so does the chart: how many levels of blocks it draws.
        diskModel.chartDepth = scanSettings.chartDepth
        chart.depth = scanSettings.chartDepth
        window.chartLayout = scanSettings.chartLayout
        try { dockState.layout = JSON.parse(scanSettings.dockLayout) } catch (e) {}
        // The column widths the user dragged last time, then the stores may
        // persist again.
        window.loadColumnWidths()
        window.columnsLoaded = true
        verticalSplit.applyLayoutOrder()
        if (window.chartLayout === "dock") Qt.callLater(dockWorkspace.refresh)
    }

    // ---- table columns ------------------------------------------------------
    // One description per table, in drawing order: the key into the width
    // store, the header text, the default width, the narrowest it may be
    // dragged, and (where a click sorts) the sort key. Headers, rows and the
    // width budget all read this, so a column cannot drift out of step with its
    // own header.
    // Defaults sum to 676, inside the tree pane a 1280 window gives it, so
    // the name column is not squeezed on first open. A saved drag still wins.
    readonly property var treeColumnSpec: [
        { key: "name", label: "文件夹 / 名称", width: 200, min: 140 },
        { key: "pct", label: "% 父级", width: 64, min: 48 },
        { key: "size", label: "大小", width: 72, min: 60, sort: 0 },
        { key: "alloc", label: "已分配", width: 72, min: 64 },
        { key: "items", label: "项目", width: 52, min: 52 },
        { key: "files", label: "文件", width: 52, min: 52 },
        { key: "dirs", label: "文件夹", width: 52, min: 48 },
        { key: "modified", label: "修改时间", width: 100, min: 92, sort: 2 },
        { key: "attributes", label: "属性", width: 52, min: 52 }
    ]
    readonly property var fileColumnSpec: [
        { key: "name", label: "名称", width: 240, min: 140 },
        { key: "pct", label: "% 父级", width: 82, min: 48 },
        { key: "size", label: "大小", width: 84, min: 60 },
        { key: "alloc", label: "已分配", width: 84, min: 64 },
        { key: "modified", label: "修改时间", width: 118, min: 92 },
        { key: "attributes", label: "属性", width: 64, min: 52 }
    ]
    readonly property var legendColumnSpec: [
        { key: "name", label: "扩展名", width: 150, min: 100 },
        { key: "percent", label: "占比", width: 56, min: 40 },
        { key: "size", label: "大小", width: 66, min: 48 },
        { key: "alloc", label: "已分配", width: 66, min: 48 },
        { key: "files", label: "文件数", width: 52, min: 40 }
    ]

    function columnSpec(panel) {
        return panel === "tree" ? treeColumnSpec : (panel === "file" ? fileColumnSpec : legendColumnSpec)
    }
    function columnStore(panel) {
        return panel === "tree" ? columns : (panel === "file" ? fileColumns : legendColumns)
    }
    function columnPane(panel) {
        return panel === "tree" ? treePane : (panel === "file" ? filePane : legendPane)
    }
    // The visible columns always fill the pane. Resizing transfers width to or
    // from the adjacent column; the name column absorbs pane-size changes.
    function visibleColumnKeys(panel) {
        return columnSpec(panel).filter(function(column) { return !columnHidden(column.key) }).map(function(column) { return column.key })
    }
    function columnResizable(panel, key) {
        const keys = visibleColumnKeys(panel)
        return keys.indexOf(key) >= 0 && keys.indexOf(key) < keys.length - 1
    }
    function requestedColumnWidths(panel) {
        const keys = visibleColumnKeys(panel)
        const store = columnStore(panel)
        const widths = ({})
        let total = 0
        for (const key of keys) {
            widths[key] = Math.max(0, store.width(key))
            total += widths[key]
        }
        return { keys: keys, widths: widths, total: total }
    }
    function columnLayout(panel) {
        const requested = requestedColumnWidths(panel)
        const keys = requested.keys
        const widths = requested.widths
        const available = Math.max(0, Math.floor(columnPane(panel).width))
        const total = requested.total
        if (total > available && total > 0) {
            const factor = available / total
            let used = 0
            for (let i = 0; i < keys.length; ++i) {
                const key = keys[i]
                widths[key] = i === keys.length - 1 ? Math.max(0, available - used)
                                                     : Math.max(0, Math.floor(widths[key] * factor))
                used += widths[key]
            }
        } else if (keys.indexOf("name") >= 0) {
            let fixed = 0
            for (const key of keys) if (key !== "name") fixed += widths[key]
            widths.name = Math.max(0, available - fixed)
        }
        return widths
    }
    function columnWidth(panel, key) { return columnLayout(panel)[key] || 0 }
    function nameWidth(panel) { return columnWidth(panel, "name") }
    function resizeColumn(panel, key, delta) {
        const requested = requestedColumnWidths(panel)
        const keys = requested.keys
        const at = keys.indexOf(key)
        if (at < 0 || keys.length < 2 || !delta) return
        const neighborAt = at < keys.length - 1 ? at + 1 : at - 1
        const neighbor = keys[neighborAt]
        const available = Math.max(0, Math.floor(columnPane(panel).width))
        const scale = requested.total > available && available > 0 ? requested.total / available : 1
        const requestedDelta = delta * scale
        const applied = Math.max(-requested.widths[key], Math.min(requested.widths[neighbor], requestedDelta))
        const store = columnStore(panel)
        const next = Object.assign({}, store.widths)
        for (const visibleKey of keys) next[visibleKey] = requested.widths[visibleKey]
        if (key === "name") {
            next[neighbor] = Math.max(0, requested.widths[neighbor] - applied)
        } else if (neighbor === "name") {
            next[key] = Math.max(0, requested.widths[key] + applied)
        } else {
            next[key] = Math.max(0, requested.widths[key] + applied)
            next[neighbor] = Math.max(0, requested.widths[neighbor] - applied)
        }
        store.setWidths(next)
    }
    function widestName(panel) {
        const textMetrics = Qt.createQmlObject('import QtQuick; TextMetrics {}', window)
        textMetrics.font.family = metrics.fontFamily
        textMetrics.font.pixelSize = metrics.caption
        let widest = textMetrics.advanceWidth
        if (panel === "legend") {
            for (const row of diskModel.extensionRows) {
                textMetrics.text = (row.ext.length ? row.ext : "(无)") + "  " + row.label
                widest = Math.max(widest, textMetrics.advanceWidth)
            }
        } else {
            const model = panel === "tree" ? diskModel : fileListModel
            const count = Math.min(model.rowCount(), 250)
            for (let row = 0; row < count; ++row) {
                const name = model.data(model.index(row, 0), Qt.UserRole + 2)
                textMetrics.text = name === undefined ? "" : String(name)
                widest = Math.max(widest, textMetrics.advanceWidth)
            }
        }
        textMetrics.destroy()
        return Math.ceil(widest + 42)
    }
    function columnRole(panel, key) {
        if (panel === "tree") return ({size: 4, pct: 5, alloc: 11, items: 14, files: 15, dirs: 16, modified: 17, attributes: 19})[key]
        if (panel === "file") return ({size: 4, pct: 5, alloc: 8, modified: 12, attributes: 14})[key]
        return undefined
    }
    function columnText(panel, key, value) {
        if (key === "pct") return Number(value).toFixed(1) + "%"
        return value === undefined || value === null ? "" : String(value)
    }
    function autoFitWidth(panel, key) {
        if (key === "name") return widestName(panel)
        if (panel === "legend") {
            const values = diskModel.extensionRows
            let widest = 0
            for (const row of values) {
                const value = key === "percent" ? Number(row.percent).toFixed(1) + "%"
                             : key === "size" ? row.sizeText
                             : key === "alloc" ? row.allocText
                             : key === "files" ? diskModel.formatCount(row.files) : ""
                const textMetrics = Qt.createQmlObject('import QtQuick; TextMetrics {}', window)
                textMetrics.font.family = metrics.fontFamily
                textMetrics.font.pixelSize = metrics.small
                textMetrics.text = String(value)
                widest = Math.max(widest, textMetrics.advanceWidth)
                textMetrics.destroy()
            }
            return Math.ceil(widest + 20)
        }
        const model = panel === "tree" ? diskModel : fileListModel
        const role = columnRole(panel, key)
        if (role === undefined) return columnStore(panel).width(key)
        const count = Math.min(model.rowCount(), 250)
        const textMetrics = Qt.createQmlObject('import QtQuick; TextMetrics {}', window)
        textMetrics.font.family = metrics.fontFamily
        textMetrics.font.pixelSize = metrics.small
        let widest = 0
        for (let row = 0; row < count; ++row) {
            textMetrics.text = columnText(panel, key, model.data(model.index(row, 0), Qt.UserRole + role))
            widest = Math.max(widest, textMetrics.advanceWidth)
        }
        textMetrics.destroy()
        return Math.ceil(widest + 20)
    }
    function autoFitColumn(panel, key) {
        resizeColumn(panel, key, autoFitWidth(panel, key) - columnWidth(panel, key))
    }
    function tableContentWidth(panel) {
        return Math.max(0, Math.floor(columnPane(panel).width))
    }
    // A column is drawn only while the fixed columns to its left still leave
    // the name column its minimum. That replaces the hand-tuned thresholds the
    // file panel used to carry, and it now applies to every table.
    function columnVisible(panel, key) {
        if (key === "name") return true
        if (columnHidden(key)) return false
        return true
    }
    function gridColor() { return Qt.rgba(theme.border.r, theme.border.g, theme.border.b, 0.5) }

    // Which columns the user switched off. The name column is not in the list:
    // it never goes away.
    function columnHidden(key) { return scanSettings.hiddenColumns.indexOf(key) >= 0 }
    function toggleColumn(key) {
        const hidden = scanSettings.hiddenColumns.slice()
        const at = hidden.indexOf(key)
        if (at >= 0) hidden.splice(at, 1)
        else hidden.push(key)
        scanSettings.hiddenColumns = hidden
        scanSettings.save()
    }

    // Widths: the stores hold what was dragged, ScanSettings keeps the three of
    // them between runs, and a reset drops the overrides back to the spec.
    property bool columnsLoaded: false
    function specDefaults(spec) {
        const map = ({})
        for (let i = 0; i < spec.length; ++i) map[spec[i].key] = spec[i].width
        return map
    }
    function loadColumnWidths() {
        let saved = {}
        try { saved = JSON.parse(scanSettings.columnWidths) || {} } catch (e) { saved = {} }
        columns.widths = saved.tree || ({})
        fileColumns.widths = saved.file || ({})
        legendColumns.widths = saved.legend || ({})
    }
    function saveColumnWidths() {
        if (!columnsLoaded) return
        widthSaveTimer.restart()
    }
    function persistColumnWidths() {
        if (!columnsLoaded) return
        scanSettings.columnWidths = JSON.stringify({
            tree: columns.widths,
            file: fileColumns.widths,
            legend: legendColumns.widths
        })
        scanSettings.save()
    }
    function resetColumnWidths() {
        columns.resetAll()
        fileColumns.resetAll()
        legendColumns.resetAll()
        persistColumnWidths()
    }
    Timer {
        id: widthSaveTimer
        interval: 300
        repeat: false
        onTriggered: window.persistColumnWidths()
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
    property string chartHighlightExtension: ""
    function chartHighlight() {
        if (chartHighlightExtension === "") return ""
        return chartHighlightExtension.length ? chartHighlightExtension : "(none)"
    }
    // Which picture the chart draws: 0 treemap, 1 sunburst. The startup value
    // is the host's default view; the toolbar switches it for this window.
    property int chartKind: diskViewMode
    // Address-bar suggestions from WinShun's own index (the ws CLI).
    property var pathSuggestions: []
    // The two panels' arrangement: "tablesTop" (default), "tablesBottom",
    // "tablesLeft" or "tablesRight". The tables keep their tree|legend split,
    // turned to match (side by side above/below, stacked left/right).
    property string chartLayout: "tablesTop"
    // "" or the pane that fills the window ("tables" / "chart").
    property string maximizedPane: ""
    property bool treeCollapsed: false
    property bool fileCollapsed: false
    property bool legendCollapsed: false
    property bool chartCollapsed: false
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
        dockState.reset()
        passed = dockState.movePane("files", "tree", "center") && passed
        passed = dockState.movePane("chart", "tree", "center") && passed
        passed = dockState.movePane("legend", "tree", "center") && passed
        passed = dockState.movePane("legend", "legend", "left") && passed
        let tabGroups = 0
        for (const placement of dockState.placements(900, 600))
            if (placement.type === "tabs") tabGroups += 1
        passed = tabGroups >= 2 && passed
        dockState.reset()
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
        function onStarted(path) {
            window.committedPath = path
            pathField.text = path
            diskModel.clear()
        }
        function onPartialTree(tree) { diskModel.loadFlat(tree, true) }
        // The finished tree is handed to the model from C++ by move, so the
        // multi-million-node result is not copied through a QML signal.
        function onCancelled() { diskModel.setPartial(true) }
        function onFailed(message) { diskModel.setPartial(true) }
    }

    Connections {
        target: diskModel
        function onCurrentPathChanged() {
            if (diskModel.currentPath.length > 0 && scanService.state !== "scanning") {
                window.committedPath = diskModel.currentPath
                pathField.text = diskModel.currentPath
            }
        }
        function onSelectedChanged() {
            if (diskModel.selectedRow >= 0) list.positionViewAtIndex(diskModel.selectedRow, ListView.Contain)
        }
        function onListChanged() {
            // Navigating clears the filter in the model, so the field has to
            // follow it.
            if (filterField.text !== fileListModel.filter) filterField.text = fileListModel.filter
        }
    }

    ScanSettingsDialog { id: settingsDialog; objectName: "scanSettingsDialog" }
    ThemeSettingsDialog { id: themeDialog }
    ScanIssuesDialog { id: issuesDialog; objectName: "scanIssuesDialog" }

    function targetPath() { return contextMenu.targetId.length ? diskModel.pathForNode(contextMenu.targetId) : contextMenu.targetPath }
    function targetAllowed() {
        const path = targetPath()
        return contextMenu.targetId.length > 0 && !diskModel.isAggregateNode(contextMenu.targetId)
               && appHost.canModifyPath(path).ok
    }
    function targetDeniedReason() {
        const path = targetPath()
        if (!path.length) return "没有可操作的项目"
        const result = appHost.canModifyPath(path)
        return result.ok ? "" : (result.reason || "此项目当前不可操作")
    }

    Dialog {
        id: destructiveDialog
        property bool permanent: false
        property string actionPath: ""
        property string actionId: ""
        property string itemName: ""
        property string sizeText: ""
        property int requestId: 0
        property bool working: false
        title: destructiveDialog.permanent ? "永久删除" : "移至回收站"
        modal: true
        width: 480
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.NoButton
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            Label { Layout.fillWidth: true; text: destructiveDialog.permanent ? "此操作不可恢复，且不会进入回收站。" : "项目将移入系统回收站，可以还原。"; color: destructiveDialog.permanent ? theme.error : theme.text; font.pixelSize: metrics.caption; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
            Label { Layout.fillWidth: true; text: destructiveDialog.actionPath + (destructiveDialog.sizeText.length ? "  ·  " + destructiveDialog.sizeText : ""); color: theme.textMuted; elide: Text.ElideMiddle; font.pixelSize: metrics.small }
            Label { visible: destructiveDialog.permanent; text: "请输入：" + destructiveDialog.itemName; color: theme.text; font.pixelSize: metrics.caption }
            TextField { id: confirmation; visible: destructiveDialog.permanent; Layout.fillWidth: true; placeholderText: destructiveDialog.itemName; font.pixelSize: metrics.caption }
            Label { Layout.fillWidth: true; text: destructiveDialog.permanent ? "永久删除" : "移至回收站"; color: destructiveDialog.permanent ? theme.error : theme.text; font.pixelSize: metrics.small }
        }
        footer: RowLayout {
            spacing: metrics.gap
            Item { Layout.fillWidth: true }
            Button {
                text: destructiveDialog.permanent ? "永久删除" : "移至回收站"
                enabled: !destructiveDialog.working && (!destructiveDialog.permanent || confirmation.text.localeCompare(destructiveDialog.itemName, Qt.CaseInsensitive) === 0)
                onClicked: {
                    destructiveDialog.working = true
                    destructiveDialog.requestId += 1
                    appHost.performFileActionAsync(destructiveDialog.actionPath, destructiveDialog.permanent,
                                                   confirmation.text, destructiveDialog.actionId, destructiveDialog.requestId)
                }
            }
            Button { text: "取消"; enabled: !destructiveDialog.working; onClicked: destructiveDialog.close() }
        }
    }

    function parentPath(path) {
        let value = String(path || "").replace(/[\\\/]+$/, "")
        if (!value.length) return ""
        if (/^[A-Za-z]:$/.test(value)) return value + "\\"
        const slash = Math.max(value.lastIndexOf("\\"), value.lastIndexOf("/"))
        if (slash < 0) return ""
        const parent = value.slice(0, slash)
        return /^[A-Za-z]:$/.test(parent) ? parent + "\\" : parent
    }
    function samePath(left, right) {
        return String(left || "").replace(/[\\\/]+$/, "").toLowerCase()
            === String(right || "").replace(/[\\\/]+$/, "").toLowerCase()
    }
    function fileToolbarFits() {
        return filePane.height >= metrics.paneHeader + metrics.headerRow * 2 + metrics.row
    }
    function startScanAfterDelete(rootPath) {
        const parent = parentPath(rootPath)
        if (!parent.length) return false
        committedPath = parent
        pathField.text = parent
        pathSuggestions.rows = []
        searchTimer.stop()
        return scanService.start(parent)
    }
    function prepareFixtureVisualState(state) {
        if (state === "drives") {
            driveBox.popup.open()
            return
        }
        if (state === "resized-columns") {
            columns.setWidth("size", 132)
            return
        }
        if (state === "address-focus") {
            pathField.forceActiveFocus()
            pathSuggestions.rows = []
            return
        }
        if (state === "dock-tabs") {
            window.chartLayout = "dock"
            window.detachForDock()
            dockState.layout = ({type: "tabs", panes: ["tree", "files", "chart", "legend"], active: "tree"})
            Qt.callLater(dockWorkspace.refresh)
            return
        }
        if (state === "dock-compass") {
            window.chartLayout = "dock"
            window.detachForDock()
            dockState.reset()
            dockWorkspace.refresh()
            dockWorkspace.dragged = "chart"
            dockWorkspace.target = "tree"
            dockWorkspace.edge = "left"
            return
        }
        if (state === "columns-menu") {
            columnsMenu.popup(filePaneHeader, filePaneHeader.width - metrics.control, filePaneHeader.height)
            return
        }
        if (state === "file-filter") {
            filePaneHeader.filterField.text = "bin"
            filePaneHeader.filterField.forceActiveFocus()
            return
        }
        if (state === "extension-highlight") {
            window.chartHighlightExtension = ".bin"
            return
        }
        if (state === "suggestions") {
            pathField.text = ""
            pathField.forceActiveFocus()
            pathSuggestions.rows = appHost.defaultPaths(committedPath)
            pathSuggestions.selectedIndex = 0
            return
        }
        if (state === "columns") {
            columns.setWidth("name", 480)
            list.contentX = 160
            return
        }
        let targetId = diskModel.selectedId
        if (!targetId.length && diskModel.rowCount() > 0)
            targetId = diskModel.data(diskModel.index(0, 0), Qt.UserRole + 1)
        if (!targetId.length) return
        contextMenu.targetId = targetId
        contextMenu.targetPath = diskModel.pathForNode(targetId)
        if (state === "context-menu") {
            contextMenu.popup(window.contentItem, Qt.point(100, 100))
        } else if (state === "delete-confirmation") {
            destructiveDialog.permanent = true
            destructiveDialog.actionPath = contextMenu.targetPath
            destructiveDialog.actionId = targetId
            destructiveDialog.itemName = contextMenu.targetPath.split(/[\\/]/).pop()
            destructiveDialog.sizeText = diskModel.formatSize(diskModel.sizeForNode(targetId))
            destructiveDialog.working = true
            confirmation.text = ""
            destructiveDialog.open()
            destructiveDialog.visible = true
            destructiveDialog.forceActiveFocus()
        }
    }
    function acceptSuggestion(entry) {
        const target = entry.dir ? entry.path : entry.parent
        acceptingSuggestion = true
        committedPath = target
        pathField.text = target
        pathSuggestions.rows = []
        pathSuggestions.selectedIndex = -1
        searchTimer.stop()
        scanService.start(target)
        acceptingSuggestion = false
    }

    Connections {
        target: appHost
        function onPathSuggestionsReady(requestId, rows) {
            if (requestId !== window.suggestionRequestId) return
            pathSuggestions.rows = rows
            pathSuggestions.selectedIndex = -1
        }
        function onFileActionFinished(requestId, nodeId, result) {
            if (requestId !== destructiveDialog.requestId || nodeId !== destructiveDialog.actionId) return
            destructiveDialog.working = false
            if (!result.ok) {
                window.notice = result.error
                return
            }
            const deletedPath = destructiveDialog.actionPath
            const removedRoot = samePath(deletedPath, scanService.rootPath)
            diskModel.removeNode(nodeId)
            if (removedRoot) {
                window.notice = destructiveDialog.permanent ? "已永久删除，正在扫描上级目录" : "已移至回收站，正在扫描上级目录"
                window.startScanAfterDelete(deletedPath)
            } else {
                window.notice = destructiveDialog.permanent ? "已永久删除" : "已移至回收站"
            }
            destructiveDialog.close()
        }
    }

    Menu {
        id: contextMenu
        property string targetPath: ""
        property string targetId: ""
        MenuItem {
            text: "打开"
            enabled: contextMenu.targetId.length > 0 && !diskModel.isAggregateNode(contextMenu.targetId)
            onClicked: diskModel.isDirectoryNode(contextMenu.targetId)
                       ? diskModel.enterNode(contextMenu.targetId) : appHost.openPath(targetPath())
        }
        MenuItem {
            text: "复制完整路径"
            enabled: targetPath().length > 0 && !diskModel.isAggregateNode(contextMenu.targetId)
            onClicked: appHost.copyToClipboard(targetPath())
        }
        MenuItem {
            text: "复制文件"
            enabled: targetAllowed()
            onClicked: appHost.copyFilesToClipboard(targetPath(), false)
        }
        MenuItem {
            text: "剪切文件"
            enabled: targetAllowed()
            onClicked: appHost.copyFilesToClipboard(targetPath(), true)
        }
        MenuItem {
            text: "在资源管理器中显示"
            enabled: targetPath().length > 0 && !diskModel.isAggregateNode(contextMenu.targetId)
            onClicked: appHost.revealInExplorer(targetPath())
        }
        MenuItem {
            text: "在树中定位"
            enabled: contextMenu.targetId.length > 0
            onClicked: diskModel.revealNode(contextMenu.targetId)
        }
        MenuItem { text: "属性"; enabled: targetPath().length > 0 && !diskModel.isAggregateNode(contextMenu.targetId); onClicked: appHost.showProperties(targetPath()) }
        MenuSeparator {}
        MenuItem {
            text: "移至回收站…"
            enabled: targetAllowed() && scanService.state !== "scanning"
            ToolTip.visible: hovered && !enabled
            ToolTip.text: scanService.state === "scanning" ? "扫描完成后才能操作" : targetDeniedReason()
            onClicked: { destructiveDialog.permanent = false; destructiveDialog.actionPath = targetPath(); destructiveDialog.actionId = contextMenu.targetId; destructiveDialog.sizeText = diskModel.formatSize(diskModel.sizeForNode(contextMenu.targetId)); destructiveDialog.open() }
        }
        MenuItem {
            text: "永久删除…"
            enabled: targetAllowed() && scanService.state !== "scanning"
            ToolTip.visible: hovered && !enabled
            ToolTip.text: scanService.state === "scanning" ? "扫描完成后才能操作" : targetDeniedReason()
            onClicked: { destructiveDialog.permanent = true; destructiveDialog.actionPath = targetPath(); destructiveDialog.actionId = contextMenu.targetId; destructiveDialog.sizeText = diskModel.formatSize(diskModel.sizeForNode(contextMenu.targetId)); destructiveDialog.itemName = targetPath().split(/[\\/]/).pop(); confirmation.text = ""; destructiveDialog.open() }
        }
    }

    Timer {
        id: columnsCloseTimer
        interval: 350
        repeat: false
        onTriggered: if (columnsMenu.visible && !columnsHover.hovered) columnsMenu.close()
    }

    Menu {
        id: columnsMenu
        property bool wasHovered: false
        onOpened: {
            wasHovered = false
            columnsCloseTimer.interval = 650
            columnsCloseTimer.restart()
        }
        onClosed: { wasHovered = false; columnsCloseTimer.stop() }
        HoverHandler {
            id: columnsHover
            onHoveredChanged: {
                if (hovered) {
                    columnsMenu.wasHovered = true
                    columnsCloseTimer.stop()
                } else if (columnsMenu.wasHovered) {
                    columnsCloseTimer.interval = 350
                    columnsCloseTimer.restart()
                }
            }
        }
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
                onTriggered: window.toggleColumn(modelData.key)
            }
        }
        MenuSeparator { }
        MenuItem { text: "列宽复位"; onTriggered: window.resetColumnWidths() }
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
        fileListModel.filter = ""; filterField.text = ""
    } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            anchors.fill: parent
            z: -1
            color: theme.backdrop ? theme.surface : theme.window
        }

        // ---- drive, path and scan controls -----------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: theme.surface
            border.color: theme.border
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 8
                // Location first: a drive picker, then the editable path, then
                // the system folder picker. Enter in the field starts the scan.
                ThemedComboBox {
                    id: driveBox
                    implicitHeight: 28
                    font.pixelSize: metrics.caption
                    model: appHost.drives()
                    textRole: "label"
                    ToolTip.visible: hovered
                    ToolTip.text: "切换磁盘"
                    function sync() {
                        const drives = appHost.drives()
                        const current = appHost.diskForPath(window.committedPath)
                        for (let i = 0; i < drives.length; ++i) {
                            if (samePath(drives[i].path, current.path)) { currentIndex = i; return }
                        }
                        currentIndex = 0
                    }
                    Component.onCompleted: sync()
                    onCountChanged: sync()
                    onActivated: function(index) {
                        const drive = appHost.drives()[index]
                        if (!drive) return
                        pathField.text = drive.path
                        window.committedPath = drive.path
                        scanService.start(drive.path)
                    }
                    Connections {
                        target: window
                        function onCommittedPathChanged() { driveBox.sync() }
                    }
                    // The row carries the free-space detail next to the label,
                    // and the popup is as wide as the longest of those rows.
                    function rowText(drive) { return drive.label + "    " + drive.detail }
                    // The delegate knows its own font and padding, so let it
                    // report the width it actually needs. A hidden Text measured
                    // with FontMetrics read short at startup and still elided.
                    property real popupContentWidth: 16
                    function reportRowWidth(w) {
                        // An eliding label reports a constrained implicitWidth,
                        // so add a generous margin for the padding and the
                        // glyphs that measurement rounds down.
                        if (w + 52 > popupContentWidth) popupContentWidth = w + 52
                    }
                    delegate: ItemDelegate {
                        id: driveEntry
                        required property var modelData
                        required property int index
                        width: ListView.view ? ListView.view.width : driveBox.width
                        height: 28
                        highlighted: driveBox.highlightedIndex === index
                        background: Rectangle {
                            radius: theme.radius
                            color: driveEntry.highlighted ? theme.hoverRow : "transparent"
                        }
                        contentItem: Label {
                            leftPadding: 8
                            text: driveBox.rowText(driveEntry.modelData)
                            color: theme.text
                            font.family: metrics.fontFamily
                            font.pixelSize: metrics.caption
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            onImplicitWidthChanged: driveBox.reportRowWidth(implicitWidth)
                        }
                    }
                    popup: Popup {
                        popupType: Popup.Window
                        y: driveBox.height + 4
                        width: Math.max(driveBox.width, driveBox.popupContentWidth + leftPadding + rightPadding)
                        padding: 4
                        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
                        background: Rectangle {
                            radius: theme.radius
                            color: theme.surface
                            border.width: 1
                            border.color: theme.border
                        }
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: driveBox.popup.visible ? driveBox.delegateModel : null
                            currentIndex: driveBox.highlightedIndex
                            spacing: 2
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: StyledScrollBar {}
                        }
                    }
                }
                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    implicitHeight: 28
                    leftPadding: 9
                    rightPadding: 9
                    text: initialScanPath
                    placeholderText: "输入或选择要扫描的位置"
                    selectByMouse: true
                    onTextEdited: {
                        window.suggestionRequestId += 1
                        pathSuggestions.selectedIndex = -1
                        if (text.trim().length > 0) searchTimer.restart()
                        else { searchTimer.stop(); pathSuggestions.rows = [] }
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus && !window.acceptingSuggestion && scanService.state !== "scanning") {
                            searchTimer.stop()
                            text = window.committedPath
                            pathSuggestions.rows = []
                        }
                    }
                    onAccepted: {
                        searchTimer.stop()
                        if (pathSuggestions.selectedIndex >= 0 && pathSuggestions.selectedIndex < pathSuggestions.rows.length) {
                            window.acceptSuggestion(pathSuggestions.rows[pathSuggestions.selectedIndex])
                        } else if (text.trim().length > 0) {
                            window.committedPath = text.trim()
                            pathSuggestions.rows = []
                            scanService.start(text.trim())
                        }
                    }
                    Keys.onDownPressed: function(event) {
                        if (pathSuggestions.rows.length === 0 && text.trim().length === 0) {
                            searchTimer.restart()
                            event.accepted = true
                            return
                        }
                        if (pathSuggestions.rows.length > 0) {
                            pathSuggestions.selectedIndex = Math.min(pathSuggestions.selectedIndex + 1, pathSuggestions.rows.length - 1)
                            pathSuggestions.keepIndexVisible()
                            event.accepted = true
                        }
                    }
                    Keys.onUpPressed: function(event) {
                        if (pathSuggestions.rows.length > 0) {
                            pathSuggestions.selectedIndex = Math.max(pathSuggestions.selectedIndex - 1, 0)
                            pathSuggestions.keepIndexVisible()
                            event.accepted = true
                        }
                    }
                    Keys.onEscapePressed: function(event) {
                        if (pathSuggestions.rows.length > 0) {
                            pathSuggestions.rows = []
                            text = window.committedPath
                            event.accepted = true
                        }
                    }
                }
                PathSuggestions {
                    id: pathSuggestions
                    anchorItem: pathField
                    rows: []
                    selectedIndex: -1
                    onHovered: function(index) { selectedIndex = index }
                    onChosen: function(entry) { window.acceptSuggestion(entry) }
                }
                Timer {
                    id: searchTimer
                    interval: 250
                    onTriggered: {
                        window.suggestionRequestId += 1
                        appHost.searchPathsAsync(pathField.text, window.suggestionRequestId)
                    }
                }
                ToolButton {
                    id: browseButton
                    implicitWidth: 30
                    implicitHeight: 28
                    ToolTip.visible: hovered
                    ToolTip.text: "浏览文件夹"
                    onClicked: {
                        const picked = appHost.chooseFolder(pathField.text)
                        if (picked.length > 0) {
                            pathField.text = picked
                            scanService.start(picked)
                        }
                    }
                    contentItem: Glyph { iconName: "folder-open"; size: 16; color: theme.text }
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
                Item { Layout.fillWidth: true }
                BusyIndicator { running: scanService.state === "scanning"; visible: running; implicitWidth: metrics.icon; implicitHeight: metrics.icon }
                ToolButton {
                    implicitWidth: metrics.control
                    implicitHeight: metrics.control
                    ToolTip.visible: hovered
                    ToolTip.text: "主题设置"
                    onClicked: themeDialog.open()
                    contentItem: Glyph { iconName: "paint-brush"; size: metrics.icon; color: theme.text }
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
                    filePane.SplitView.minimumHeight = metrics.paneHeader + metrics.headerRow * 2 + metrics.row
                    filePane.SplitView.minimumWidth = -1
                    legendPane.SplitView.fillHeight = false
                    legendPane.SplitView.preferredHeight = 190
                    legendPane.SplitView.minimumHeight = metrics.paneHeader + metrics.row
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
                    SplitView.minimumHeight: window.treeCollapsed ? metrics.paneHeader : -1
                    SplitView.preferredHeight: window.treeCollapsed ? metrics.paneHeader : -1
                    PaneHeader {
                        id: treePaneHeader
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        title: "树视图"; iconName: "tree-structure"
                        showTitle: window.chartLayout !== "dock"
                        maximized: window.maximizedPane === "tables"
                        collapsed: window.treeCollapsed
                        onMaximizeRequested: window.maximizePane("tables")
                        onCollapseRequested: window.treeCollapsed = !window.treeCollapsed
                    }
                    ColumnLayout {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.top: treePaneHeader.bottom
                        spacing: 0
                        visible: !window.treeCollapsed
                        Flickable {
                            id: treeHeader
                            Layout.fillWidth: true
                            Layout.preferredHeight: metrics.headerRow
                            Layout.minimumHeight: metrics.headerRow
                            clip: true
                            contentWidth: list.contentWidth
                            contentHeight: height
                            contentX: list.contentX
                            interactive: false
                            boundsBehavior: Flickable.StopAtBounds
                            Rectangle { anchors.fill: parent; color: theme.surfaceAlt; border.color: theme.border }
                            RowLayout {
                                width: treeHeader.contentWidth
                                height: treeHeader.height
                                spacing: 0
                                TableCell {
                                             cellWidth: window.nameWidth("tree")
                                        lineColor: window.gridColor()
                                        Layout.preferredWidth: window.nameWidth("tree")
                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: "文件夹 / 名称 ↓"
                                        font.pixelSize: metrics.caption; font.weight: Font.DemiBold; color: theme.textMuted
                                        elide: Text.ElideRight
                                    }
                                             ColumnHandle {
                                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                        objectName: "columnHandle:tree:name"
                                          store: window.columnStore("tree"); column: "name"
                                          visible: window.columnResizable("tree", "name")
                                          effectiveWidth: function() { return window.nameWidth("tree") }
                                          resizeBy: function(delta) { window.resizeColumn("tree", "name", delta) }
                                         autoFit: function() { window.autoFitColumn("tree", "name") }
                                    }
                                }
                                Repeater {
                                    objectName: "treeColumnRepeater"
                                    model: window.treeColumnSpec.slice(1)
                                    delegate: TableCell {
                                        cellWidth: window.columnWidth("tree", modelData.key)
                                        visible: window.columnVisible("tree", modelData.key)
                                        lineColor: window.gridColor()
                                        Label {
                                            anchors.fill: parent
                                            anchors.rightMargin: 8
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: modelData.label + (modelData.sort !== undefined && diskModel.sortKey === modelData.sort
                                                                     ? (diskModel.ascending ? " ↑" : " ↓") : "")
                                         font.pixelSize: metrics.caption; font.weight: Font.DemiBold; color: theme.textMuted
                                            elide: Text.ElideRight
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: modelData.sort !== undefined && modelData.sort !== 1
                                            cursorShape: modelData.sort !== undefined ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: diskModel.toggleSort(modelData.sort)
                                        }
                                        ColumnHandle {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            objectName: "columnHandle:tree:" + modelData.key
                                              store: window.columnStore("tree")
                                              column: modelData.key
                                              visible: window.columnResizable("tree", modelData.key)
                                              resizeBy: function(delta) { window.resizeColumn("tree", modelData.key, delta) }
                                             autoFit: function() { window.autoFitColumn("tree", modelData.key) }
                                        }
                                    }
                                }
                            }
                        }
                        ListView {
                            id: list
                            objectName: "diskViewList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: window.tableContentWidth("tree")
                             ScrollBar.horizontal: StyledScrollBar { policy: ScrollBar.AlwaysOff }
                             ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AsNeeded }
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
                                width: ListView.view.contentWidth
                                height: metrics.row
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
                                contentItem: Item {
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 0
                                        TableCell {
                                            cellWidth: window.nameWidth("tree")
                                            lineColor: window.gridColor()
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 6 + rowItem.depth * 12
                                                anchors.rightMargin: 6
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
                                                    color: theme.text; font.pixelSize: metrics.caption
                                                }
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "pct")
                                            cellWidth: window.columnWidth("tree", "pct")
                                            lineColor: window.gridColor()
                                              Rectangle {
                                                  anchors.left: parent.left
                                                  anchors.top: parent.top
                                                  anchors.bottom: parent.bottom
                                                  width: Math.max(rowItem.percent > 0 ? 2 : 0,
                                                                 Math.min(1, rowItem.percent / 100) * parent.width)
                                                  color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b,
                                                                window.dark ? 0.30 : 0.18)
                                              }
                                            Label {
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                verticalAlignment: Text.AlignVCenter
                                                text: rowItem.percent.toFixed(1) + "%"
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "size")
                                            cellWidth: window.columnWidth("tree", "size")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.sizeText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "alloc")
                                            cellWidth: window.columnWidth("tree", "alloc")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.allocText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "items")
                                            cellWidth: window.columnWidth("tree", "items")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.directory ? diskModel.formatCount(rowItem.items) : "—"
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "files")
                                            cellWidth: window.columnWidth("tree", "files")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.directory ? diskModel.formatCount(rowItem.files) : "—"
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "dirs")
                                            cellWidth: window.columnWidth("tree", "dirs")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.directory ? diskModel.formatCount(rowItem.dirs) : "—"
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "modified")
                                            cellWidth: window.columnWidth("tree", "modified")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.modifiedText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("tree", "attributes")
                                            cellWidth: window.columnWidth("tree", "attributes")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: rowItem.attributes
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                    }
                                    // One line under the whole row: the cells carry
                                    // the vertical rules, this closes the band.
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: window.gridColor()
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
                        SplitView.minimumHeight: window.fileCollapsed ? metrics.paneHeader : metrics.paneHeader + metrics.headerRow * 2 + metrics.row
                        SplitView.preferredHeight: window.fileCollapsed ? metrics.paneHeader : -1
                        PaneHeader {
                            id: filePaneHeader
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            title: "文件视图"; iconName: "rows"
                            showTitle: window.chartLayout !== "dock"
                            maximized: window.maximizedPane === "tables"
                            collapsed: window.fileCollapsed
                            showFilter: true
                            showColumnsButton: true
                            onMaximizeRequested: window.maximizePane("tables")
                            onCollapseRequested: window.fileCollapsed = !window.fileCollapsed
                            onColumnsRequested: function(anchor) { columnsMenu.popup(anchor, 0, anchor.height) }
                        }
                        ColumnLayout {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.top: filePaneHeader.bottom
                            spacing: 0
                            visible: !window.fileCollapsed
                            Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: metrics.headerRow
                            Layout.minimumHeight: metrics.headerRow
                            visible: window.fileToolbarFits()
                            color: theme.surfaceAlt
                            border.color: theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: "文件列表" + (fileListModel.wholeTree ? " · 整个扫描"
                                            : fileListModel.recursive ? " · 含子目录" : "")
                                    font.pixelSize: metrics.caption; font.weight: Font.DemiBold; color: theme.textMuted
                                }
                                // Where the files come from: the folder the
                                // chart is on, or the whole scan.
                                ThemedComboBox {
                                    implicitHeight: 22
                                    model: ["当前", "整个扫描"]
                                    currentIndex: fileListModel.wholeTree ? 1 : 0
                                    onActivated: {
                                        fileListModel.wholeTree = (index === 1)
                                        scanSettings.wholeTree = fileListModel.wholeTree
                                        scanSettings.save()
                                    }
                                }
                                // Walk the folder's subtree. Off by default: a
                                // big folder's recursive listing is what made
                                // the first paint slow.
                                ToolButton {
                                    id: recursiveButton
                                    implicitWidth: 26
                                    implicitHeight: 22
                                    checkable: true
                                    checked: fileListModel.recursive
                                    ToolTip.visible: hovered
                                    ToolTip.text: "包含子目录（大目录会慢）"
                                    onToggled: {
                                        fileListModel.recursive = checked
                                        scanSettings.fileRecursive = checked
                                        scanSettings.save()
                                    }
                                    background: Rectangle {
                                        radius: 3
                                        color: recursiveButton.checked ? theme.selection : "transparent"
                                        border.color: theme.border
                                    }
                                    contentItem: Glyph {
                                        iconName: "folders"
                                        size: 13
                                        color: recursiveButton.checked ? theme.text : theme.textMuted
                                    }
                                }
                                // How loosely a text matches: a substring of
                                // the path, or the fuzzy subsequence of the
                                // name.
                                ThemedComboBox {
                                    implicitHeight: 22
                                    model: ["子串", "模糊"]
                                    currentIndex: fileListModel.fuzzyMatch ? 1 : 0
                                    onActivated: {
                                        fileListModel.fuzzyMatch = (index === 1)
                                        scanSettings.fuzzyMatch = fileListModel.fuzzyMatch
                                        scanSettings.save()
                                    }
                                }
                            }
                        }
                        // Column headers, with the same drag strips as the
                        // tree table; widths live in fileColumns.
                        Flickable {
                            id: fileHeader
                            Layout.fillWidth: true
                            Layout.preferredHeight: metrics.headerRow
                            Layout.minimumHeight: metrics.headerRow
                            clip: true
                            contentWidth: fileList.contentWidth
                            contentHeight: height
                            contentX: fileList.contentX
                            interactive: false
                            boundsBehavior: Flickable.StopAtBounds
                            Rectangle { anchors.fill: parent; color: theme.surfaceAlt; border.color: theme.border }
                            RowLayout {
                                width: fileHeader.contentWidth
                                height: fileHeader.height
                                spacing: 0
                                TableCell {
                                    cellWidth: window.nameWidth("file")
                                    lineColor: window.gridColor()
                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: "名称"; font.pixelSize: metrics.caption; color: theme.textMuted; elide: Text.ElideRight
                                    }
                                         ColumnHandle {
                                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                        objectName: "columnHandle:file:name"
                                          store: window.columnStore("file"); column: "name"
                                          visible: window.columnResizable("file", "name")
                                          effectiveWidth: function() { return window.nameWidth("file") }
                                          resizeBy: function(delta) { window.resizeColumn("file", "name", delta) }
                                         autoFit: function() { window.autoFitColumn("file", "name") }
                                    }
                                }
                                Repeater {
                                    model: window.fileColumnSpec.slice(1)
                                    delegate: TableCell {
                                        cellWidth: window.columnWidth("file", modelData.key)
                                        visible: window.columnVisible("file", modelData.key)
                                        lineColor: window.gridColor()
                                        Label {
                                            anchors.fill: parent
                                            anchors.rightMargin: 8
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: modelData.label
                                            font.pixelSize: metrics.caption; color: theme.textMuted
                                            elide: Text.ElideRight
                                        }
                                        ColumnHandle {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            objectName: "columnHandle:file:" + modelData.key
                                              store: window.columnStore("file")
                                              column: modelData.key
                                              visible: window.columnResizable("file", modelData.key)
                                              resizeBy: function(delta) { window.resizeColumn("file", modelData.key, delta) }
                                             autoFit: function() { window.autoFitColumn("file", modelData.key) }
                                        }
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
                            contentWidth: window.tableContentWidth("file")
                             ScrollBar.horizontal: StyledScrollBar { policy: ScrollBar.AlwaysOff }
                             ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AsNeeded }
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
                                width: ListView.view.contentWidth
                                height: metrics.fileRow
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
                                contentItem: Item {
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 0
                                        TableCell {
                                            cellWidth: window.nameWidth("file")
                                            lineColor: window.gridColor()
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                spacing: 4
                                                Rectangle { Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 2; color: fileRow.extColor }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: fileRow.name
                                                    elide: Text.ElideRight
                                                    color: theme.text; font.pixelSize: metrics.small
                                                }
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("file", "pct")
                                            cellWidth: window.columnWidth("file", "pct")
                                            lineColor: window.gridColor()
                                              Rectangle {
                                                  anchors.left: parent.left
                                                  anchors.top: parent.top
                                                  anchors.bottom: parent.bottom
                                                  width: Math.max(fileRow.percent > 0 ? 2 : 0,
                                                                 Math.min(1, fileRow.percent / 100) * parent.width)
                                                  color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b,
                                                                window.dark ? 0.30 : 0.18)
                                              }
                                            Label {
                                                anchors.fill: parent
                                                anchors.leftMargin: 6
                                                anchors.rightMargin: 6
                                                verticalAlignment: Text.AlignVCenter
                                                text: fileRow.percent.toFixed(1) + "%"
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("file", "size")
                                            cellWidth: window.columnWidth("file", "size")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: fileRow.sizeText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("file", "alloc")
                                            cellWidth: window.columnWidth("file", "alloc")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: fileRow.allocText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("file", "modified")
                                            cellWidth: window.columnWidth("file", "modified")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: fileRow.modifiedText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("file", "attributes")
                                            cellWidth: window.columnWidth("file", "attributes")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 8
                                                text: fileRow.attributes
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                    }
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: window.gridColor()
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    id: legendPane
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 250
                    SplitView.minimumHeight: window.legendCollapsed ? metrics.headerRow : -1
                    SplitView.preferredHeight: window.legendCollapsed ? metrics.headerRow : -1
                    PaneHeader {
                        id: legendPaneHeader
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        title: "扩展名视图"; iconName: "tag"
                        showTitle: window.chartLayout !== "dock"
                        maximized: window.maximizedPane === "tables"
                        collapsed: window.legendCollapsed
                        onMaximizeRequested: window.maximizePane("tables")
                        onCollapseRequested: window.legendCollapsed = !window.legendCollapsed
                    }
                    ColumnLayout {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.top: legendPaneHeader.bottom
                        spacing: 0
                        visible: !window.legendCollapsed
                        Flickable {
                            id: legendHeader
                            Layout.fillWidth: true
                            Layout.preferredHeight: metrics.headerRow
                            Layout.minimumHeight: metrics.headerRow
                            clip: true
                            contentWidth: legend.contentWidth
                            contentHeight: height
                            contentX: legend.contentX
                            interactive: false
                            Rectangle { anchors.fill: parent; color: theme.surfaceAlt; border.color: theme.border }
                            RowLayout {
                                width: legendHeader.contentWidth
                                height: legendHeader.height
                                spacing: 0
                                TableCell {
                                    cellWidth: window.nameWidth("legend")
                                    lineColor: window.gridColor()
                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: 26
                                        verticalAlignment: Text.AlignVCenter
                                        text: "扩展名"; font.pixelSize: metrics.caption; font.weight: Font.DemiBold; color: theme.textMuted
                                        elide: Text.ElideRight
                                    }
                                    ColumnHandle {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        objectName: "columnHandle:legend:name"
                                        store: window.columnStore("legend")
                                        column: "name"
                                        visible: window.columnResizable("legend", "name")
                                        effectiveWidth: function() { return window.nameWidth("legend") }
                                        resizeBy: function(delta) { window.resizeColumn("legend", "name", delta) }
                                        autoFit: function() { window.autoFitColumn("legend", "name") }
                                    }
                                }
                                Repeater {
                                    objectName: "legendColumnRepeater"
                                    model: window.legendColumnSpec.slice(1)
                                    delegate: TableCell {
                                        cellWidth: window.columnWidth("legend", modelData.key)
                                        visible: window.columnVisible("legend", modelData.key)
                                        lineColor: window.gridColor()
                                        Label {
                                            anchors.fill: parent
                                            anchors.rightMargin: 6
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: modelData.label
                                            font.pixelSize: metrics.caption; font.weight: Font.DemiBold; color: theme.textMuted
                                            elide: Text.ElideRight
                                        }
                                         ColumnHandle {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            objectName: "columnHandle:legend:" + modelData.key
                                              store: window.columnStore("legend")
                                              column: modelData.key
                                              visible: window.columnResizable("legend", modelData.key)
                                              resizeBy: function(delta) { window.resizeColumn("legend", modelData.key, delta) }
                                             autoFit: function() { window.autoFitColumn("legend", modelData.key) }
                                        }
                                    }
                                }
                            }
                        }
                        ListView {
                            id: legend
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: window.tableContentWidth("legend")
                             ScrollBar.horizontal: StyledScrollBar { policy: ScrollBar.AlwaysOff }
                             ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AsNeeded }
                            boundsBehavior: Flickable.StopAtBounds
                            model: diskModel.extensionRows
                            delegate: ItemDelegate {
                                width: ListView.view.contentWidth
                                height: metrics.row
                                padding: 0
                                // Two links in one: the row the legend picked
                                // (which also filters the file list), and the
                                // extension of whatever the chart has
                                // selected, so the panels point at each other.
                                highlighted: (window.chartHighlightExtension === modelData.ext)
                                             || (diskModel.selectedExtension.length > 0 && diskModel.selectedExtension === modelData.ext)
                                onClicked: window.chartHighlightExtension = window.chartHighlightExtension === modelData.ext ? "" : modelData.ext
                                contentItem: Item {
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 0
                                        TableCell {
                                            cellWidth: window.nameWidth("legend")
                                            lineColor: window.gridColor()
                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.leftMargin: 5
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 9; height: 9; radius: 2; color: modelData.color
                                            }
                                            Label {
                                                anchors.fill: parent
                                                anchors.leftMargin: 26
                                                anchors.rightMargin: 6
                                                verticalAlignment: Text.AlignVCenter
                                                text: (modelData.ext.length ? modelData.ext : "(无)") + "  " + modelData.label
                                                elide: Text.ElideRight
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("legend", "percent")
                                            cellWidth: window.columnWidth("legend", "percent")
                                            lineColor: window.gridColor()
                                              Rectangle {
                                                  anchors.left: parent.left
                                                  anchors.top: parent.top
                                                  anchors.bottom: parent.bottom
                                                  width: Math.max(modelData.percent > 0 ? 2 : 0,
                                                                 Math.min(1, modelData.percent / 100) * parent.width)
                                                  color: modelData.color
                                                 opacity: window.dark ? 0.30 : 0.18
                                              }
                                            Label {
                                                anchors.fill: parent
                                                anchors.rightMargin: 6
                                                verticalAlignment: Text.AlignVCenter
                                                horizontalAlignment: Text.AlignRight
                                                text: modelData.percent.toFixed(1) + "%"
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("legend", "size")
                                            cellWidth: window.columnWidth("legend", "size")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 6
                                                text: modelData.sizeText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.text; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("legend", "alloc")
                                            cellWidth: window.columnWidth("legend", "alloc")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 6
                                                text: modelData.allocText
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                        TableCell {
                                            visible: window.columnVisible("legend", "files")
                                            cellWidth: window.columnWidth("legend", "files")
                                            lineColor: window.gridColor()
                                            Label {
                                                anchors.fill: parent; anchors.rightMargin: 6
                                                text: diskModel.formatCount(modelData.files)
                                                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                                color: theme.textMuted; font.pixelSize: metrics.small
                                            }
                                        }
                                    }
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: window.gridColor()
                                    }
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
                SplitView.minimumHeight: window.chartCollapsed ? metrics.headerRow : 110
                SplitView.preferredHeight: window.chartCollapsed ? metrics.headerRow : -1
                spacing: 4

                PaneHeader {
                    Layout.fillWidth: true
                    title: "可视化视图"; iconName: window.chartKind === 0 ? "squares-four" : "chart-donut"
                    showTitle: window.chartLayout !== "dock"
                    maximized: window.maximizedPane === "chart"
                    collapsed: window.chartCollapsed
                    onMaximizeRequested: window.maximizePane("chart")
                    onCollapseRequested: window.chartCollapsed = !window.chartCollapsed
                }

                Rectangle {
                    visible: !window.chartCollapsed
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    Layout.minimumHeight: 30
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
                                            onClicked: {
                                                diskModel.navigate(modelData.id)
                                                diskModel.select(modelData.id)
                                                if (diskModel.selectedRow >= 0)
                                                    Qt.callLater(function() { list.positionViewAtIndex(diskModel.selectedRow, ListView.Contain) })
                                            }
                                        }
                                    }
                                    Label { text: "›"; font.pixelSize: 14; color: theme.textMuted; visible: index < window.crumbs.length - 1 }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "当前目录 · " + diskModel.totalText + " · " + diskModel.formatCount(diskModel.nodeItems) + " 项"
                            color: theme.textMuted; font.pixelSize: metrics.small
                        }
                    }
                }

                // The chart's own toolbar: which picture and how deep. These
                // belong to the chart, not to the scan, so they live here.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8; Layout.rightMargin: 8
                    Layout.preferredHeight: metrics.control
                    Layout.minimumHeight: 26
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
                        contentItem: Glyph { iconName: "squares-four"; size: 15; color: treemapButton.checked ? theme.text : theme.text }
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
                        contentItem: Glyph { iconName: "chart-donut"; size: 15; color: theme.text }
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
                        contentItem: Glyph { iconName: "arrow-counter-clockwise"; size: 15; color: theme.text }
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: "层级"; font.pixelSize: metrics.small; color: theme.textMuted }
                    ThemedComboBox {
                        id: depthBox
                        implicitHeight: 26
                        model: ["自动", "1", "2", "3", "4", "5"]
                        currentIndex: chart.depth
                        ToolTip.visible: hovered
                        ToolTip.text: "方块里再画几层文件夹；自动按每个文件夹的面积决定"
                        onActivated: {
                            chart.depth = index
                            diskModel.chartDepth = index
                            scanSettings.chartDepth = index
                            scanSettings.save()
                        }
                    }
                    ToolButton {
                        visible: window.chartLayout === "dock"
                        implicitWidth: 28; implicitHeight: 26
                        ToolTip.visible: hovered
                        ToolTip.text: "恢复默认 Dock 布局"
                        onClicked: dockState.reset()
                        contentItem: Glyph { iconName: "layout"; size: 15; color: theme.text }
                    }
                    Label { text: "布局"; font.pixelSize: metrics.small; color: theme.textMuted }
                    ThemedComboBox {
                        id: layoutBox
                        implicitHeight: 26
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
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        // The chart paints its own tiles; the rectangle only
                        // frames them, so on Mica it stays clear and the tiles
                        // sit on the material.
                        color: theme.backdrop ? "transparent" : theme.surface
                        border.color: theme.border
                        radius: 4
                        clip: true
                             ChartItem {
                            id: chart
                            objectName: "diskViewChart"
                            anchors.fill: parent
                            treeData: diskModel.chartTree
                            selectedId: diskModel.selectedId
                            viewMode: window.chartKind
                            // The chart's own greys follow the window's
                            // light/dark choice, including a live style switch.
                            // `window.dark`, not `theme.dark`: inside this
                            // binding `theme` is the property being set.
                            theme: window.dark ? "dark" : "light"
                            centerLabel: diskModel.currentName
                            highlightExtension: window.chartHighlight()
                             accent: window.themeTokens.accent
                             surfaceToken: theme.surfaceAlt
                             borderToken: theme.border
                             neutralToken: theme.textMuted
                             dimToken: theme.faint
                             fontFamily: metrics.fontFamily
                             fontPixels: metrics.caption
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
                            width: Math.min(280, parent.width - 16)
                            height: tip.implicitHeight + metrics.pad
                            x: Math.max(8, Math.min(parent.width - width - 8, chart.hoverPosition.x + 16))
                            y: Math.max(8, Math.min(parent.height - height - 8, chart.hoverPosition.y + 18))
                            color: theme.hoverRow; border.color: theme.border; radius: metrics.cardRadius
                            Column {
                                id: tip
                                anchors.fill: parent; anchors.margins: metrics.pad / 2; spacing: 3
                                Label { width: parent.width; text: hoverCard.info.name || ""; font.family: metrics.fontFamily; font.weight: Font.DemiBold; font.pixelSize: metrics.caption; color: theme.text; elide: Text.ElideRight }
                                Label { width: parent.width; text: hoverCard.info.path || ""; font.family: metrics.fontFamily; font.pixelSize: metrics.small; color: theme.textMuted; elide: Text.ElideMiddle; maximumLineCount: 1 }
                                Label {
                                    width: parent.width; font.family: metrics.fontFamily; font.pixelSize: metrics.small; color: theme.text
                                    text: (hoverCard.info.directory ? "文件夹" : "文件") + " · "
                                          + diskModel.formatSize(hoverCard.info.size || 0) + " · "
                                          + (hoverCard.info.percent || 0).toFixed(1) + "%"
                                }
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
            }
        } // the dock workspace

        // ---- status bar --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: metrics.row; color: theme.surface; border.color: theme.border
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                Label {
                    text: diskModel.selectedId.length ? "已选：" + (chart.hoveredInfo(diskModel.selectedId).name || "")
                                                      : "未选中"
                    color: theme.textMuted; font.pixelSize: metrics.small
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
                    font.pixelSize: metrics.small
                    elide: Text.ElideRight
                }
                Label {
                    text: chart.hoverId.length ? (chart.hoveredInfo(chart.hoverId).path || "") : ""
                    color: theme.textMuted; font.pixelSize: metrics.small
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: parent.width * 0.5
                }
            }
        }
    }

    // The three tables' widths. Only what the user dragged lives in `widths`;
    // the spec above supplies the defaults, and ScanSettings keeps all three
    // between runs.
    ColumnStore { id: columns; defaults: window.specDefaults(window.treeColumnSpec); onChanged: window.saveColumnWidths() }
    ColumnStore { id: fileColumns; defaults: window.specDefaults(window.fileColumnSpec); onChanged: window.saveColumnWidths() }
    ColumnStore { id: legendColumns; defaults: window.specDefaults(window.legendColumnSpec); onChanged: window.saveColumnWidths() }
}
