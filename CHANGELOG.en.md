# Changelog

## [QML 0.3.5] - 2026-11-16

### Fixed
- The drive switcher is a ComboBox again and no longer auto-closes on pointer exit.
- Adopt WinShun's default theme for the plugin's own fallback palette: standalone runs and the local light/dark modes now use the host's fluent Look neutrals instead of the old bluish dark table, with matching danger, tag fill, menu/window edge colours and radii (control 4, card 6, small 3).
- When the host sends only some tokens, menu background, hover and edge colours also fall back to WinShun's defaults; chart dim colours become neutral greys.

## [QML 0.3.4] - 2026-11-16

### Fixed
- Allow actions on the scan root; after successful deletion, clear the stale snapshot and scan its parent while retaining protected-system-path checks.
- Make the Extension column in Extension View resizable and auto-fit capable.
- Keep narrow-Dock display scaling out of persisted widths; resizing changes only the target and adjacent columns.
- Close the drive and column menus shortly after the pointer leaves.
- Fit PaneHeader controls to their row and hide the secondary file toolbar in short panes to prevent compressed, overlapping rows.

## [QML 0.3.3] - 2026-11-16

### Fixed
- Show the Dock compass for its target tab group, including fully merged groups.
- Anchor the column menu to the file-view toolbar instead of the window origin.
- Keep name/file filtering local to the file view; the tree outline is unaffected.
- Make extension selection highlight matching chart items without filtering the file list.
- Standardize Dock tab titles as Tree View, File View, Visualization View, and Extension View; remove the short active-tab stripe.

## [QML 0.3.2] - 2026-11-16

### Fixed
- Render page and chrome using WinShun's `background`/`layer` roles and preserve host-provided theme tokens.
- Allow the active tab to be split out after every pane has been merged into one group.
- Use the theme row height consistently for Dock tab strips and pane offsets.
- Remove resize minimums; keep table width fixed and transfer width to the adjacent column, proportionally compressing columns in narrow panes.
- Widen default percentage columns and the drive menu.

## [QML 0.3.1] - 2026-11-16

### Fixed
- Dock tabs now use a filled active background instead of a boxed outline.
- Percentage columns use low-opacity full-cell data bars with legible text above them; default percentage widths are narrower.
- Increased light-theme contrast for surfaces, dividers, icons, and scrollbars.
- Resizing a column pushes following columns right; double-clicking any divider auto-fits its contents.
- Restored the standard drive menu; address suggestions appear only after typing or an explicit Down-key request.
- Removed the orphaned “Search:” row and moved column visibility and filtering into the tree-pane header.
- Chart breadcrumbs, tree navigation, and current-folder selection stay synchronized; breadcrumb controls retain a minimum height.
- Folder title strips and frames remain visible when unselected, and large collapsed folders keep a frame.

## [QML 0.3.0] - 2026-11-16

### Added
- Theme settings for base font size, tree row height, file row height, and header row height, with a denser default layout.
- Vertical scrollbars for the three main lists and a scrollable drive picker that closes after the pointer leaves it.
- Pane title bars with collapse, maximize, restore, and double-click maximize behavior.

### Fixed
- Selected chart titles keep neutral chrome instead of reversing their text and background.
- Recycle Bin and permanent delete are enabled correctly for items under a scanned drive root.
- Percentage columns show visible bars, and automatic chart depth keeps expanding readable children after navigation.
- Column resizing uses a flexible name column and keeps headers, rows, and scrolling aligned.

## [QML 0.2.0] - 2026-11-16

### Added
- Complete item actions: open, copy path, copy/cut item, show in Explorer, locate in the tree, properties, Recycle Bin, and permanent delete with typed-name confirmation.
- Async address suggestions with keyboard selection and quick locations when the query is empty.

### Changed
- Follow WinShun appearance tokens, font family and size; remove the duplicate in-window title.
- Keep the tree size-descending; file-list sorting remains independent.
- Make names resizable and pin their width while resizing other columns; tables scroll horizontally when required.
- Export chart data breadth-first, retain omitted bytes/counts, and use absolute paths for chart actions.

### Permissions
- Added `files:write` for Recycle Bin and permanent-delete actions. WinShun asks for confirmation when upgrading.

[涓枃](CHANGELOG.md)

## [0.3.0] - 2026-11-09

### Changed
- The window now matches the HTML interaction prototype item by item: pixel
  font tokens (13 body, 13 semibold for emphasis, 12 secondary, 11 for tile
  labels), a two-level chart title instead of one 20px string, the list head
  merged into one row with the mode tabs and the filter side by side, 28px
  rows, three independently drawn columns that line up with the data, and no
  double inset around the chart, which now gets about 81% of the window height
  (613px measured at 1184x761).
- Zoom and pan no longer take buttons: the wheel, Space+drag, middle-drag and
  `0` cover the same actions, so the chart pane has no zoom/pan button row and no
  in-pane title strip.
- Status line 1 shows the selected item's name, full path, size and share, plus
  Enter / Locate / More. Line 2 shows the read-error count, the key hints and the
  chart's pixel share.
- "All ancestors" is now a command-table entry (`Command::Ancestors`) reachable
  from the context menu and Ctrl+K instead of its own button.
- In a narrow window the chart toolbar wraps to two lines (parent and current
  folder, then the view switches) and the status bar stacks the same way.

### Added
- Bounded directory workers for ordinary scanning (`parallel_scan.h`): a fixed
  worker pool feeds a single coordinator that owns the tree, so no two threads
  mutate shared nodes. Device detection starts at SSD 4, HDD 1, network 2 and
  unknown 1; the scan settings panel offers 1 / 2 / 4 / 8. More workers are not
  promised to be faster.
- Scan settings panel: scan mode, worker limit, media strategy, preview
  interval and next-scan exclusions. These settings now persist in
  diskview.json and are restored on the next start, including after a failed or
  cancelled scan.
- Pause/resume during a scan, and a rescan entry point (F5) that opens the
  settings panel instead of restarting silently.
- Parent navigation in three places: a titled "鈫?Parent" button beside the
  current folder, a fixed ".." first row in the list, and the ring centre.
  Returning restores the folder just left and its focus.
- Per-folder view memory: selection, filter, scroll position and list mode are
  restored when a folder is revisited.
- Chart zoom (25% steps to 400%), four-direction panning with bounded limits,
  space+drag and middle-drag panning, and a fit-to-window reset. Hit testing
  accounts for zoom and pan.
- Unified command table (`viewer_commands.h`) shared by the context menu, the
  command search (Ctrl+K) and the keyboard, so a command cannot behave
  differently depending on how it was invoked. Right-click and Shift+F10 open
  the same menu for chart items, list rows and the status bar.
- File operations completed: move to the Recycle Bin (IFileOperation), copy
  or cut the selected item (CF_HDROP with the Preferred DropEffect so Explorer
  pastes it, Ctrl+Shift+C / X), and delete permanently, which bypasses the
  Recycle Bin, requires reading a warning and typing the item's name, and has
  no shortcut. An incomplete snapshot no longer blocks deletions; a running
  scan and protected paths are still refused. The snapshot is updated only
  after the shell confirms. Adds the clipboard:write permission.
- Direct2D antialiased ring geometry (`viewer_drawing.h`) with a GDI fallback
  when the device or factory is unavailable.
- Non-modal information panels for scan issues, size accounting and stage
  timings, so navigation stays usable while reading them.
- MFT records awaiting parent association are counted as `pendingRecords`
  during a partial preview and only reconciled into `orphanRecords` when the
  scan completes. Excluded subtrees are counted as `excludedItems`, never as
  read errors.

### Fixed
- Scan settings were lost on every restart because diskview.json only carried
  the last path.
- The capacity and coverage dialogs were modal and blocked parent navigation.

## [0.2.0] - 2026-11-09

### Fixed

- Embed the executable icon and include the ICO and resource script in the package. Assign large and small window icons explicitly, refresh them for DPI changes and release owned handles. Include the current workspace 0.2.0 scanner and UI changes; the new processes permission requires confirmation when upgrading.

### Notes

- Local tag and package only. See docs/icon-validation.en.md for the measured coverage; no GitHub release. Restart the plugin process after upgrading.
