# Changelog

## [0.3.18] - 2026-11-18

### Fixed
- Install Qt 6.12.0 from its current SDK repository layout in the QML release workflow.
- Cover aggregated remainder bytes and single-child folders in the treemap tests.

## [0.3.17] - 2026-11-18

### Fixed
- Build, test and package the host-Qt QML plugin in the release workflow, using its own manifest and QML branch identity.
- Validate remote version tags and QML package contents before publishing immutable release assets.

## [0.3.16] - 2026-11-18

### Fixed
- File-operation context menus retain a normal horizontal width.
- Settings button labels are centered, and Apply retains its accent fill on hover.

## [QML 0.3.15] - 2026-11-18

### Fixed
- The settings window and scan-settings dialog now render Cancel as a clear secondary outline button and Apply as the primary action.
- File-operation context menus now share a styled surface and menu items with visible hover, keyboard-highlight, pressed and disabled states.
- The chart's depth popup aligns to the selector's right edge so its options stay inside the window.
- Showing Clear selection in the extension pane no longer changes the Dock tab-row height; the button stays vertically centered.
- Upgrading an older configuration with an empty exclusion list now seeds fixed-drive system folders once; later user removals and clearing remain respected.

## [QML 0.3.14] - 2026-11-18

### Added
- The plugin has its own settings window (`settingsWindow`): the card's Settings button and the launcher's "Disk view settings" entry open it, holding everything the scan-settings dialog had but building no scanner and reading no disk tree. The viewer toolbar's scan settings show the same content.
- A folder result offers "Analyze folder space" (a `files` trigger, one folder only); the path goes straight into the existing scan flow, so spaces and non-ASCII characters need no retyping.

### Changed
- Host settings now take effect: they are applied at initialize and `settingsChanged` updates the same settings object on the GUI thread, so the next scan and every open window read one value.
- Default view, scan mode and the folder-menu switch are editable again and write back through `winshun/settings/update`, only the keys that changed; a host update is never echoed back. The worker limit, media strategy and preview interval left the manifest and are stored by the plugin, while a value the host still carries for them is used as before.
- The default view is separate from the layout a window is browsing in: the default only decides what the next window opens with, and switching the layout no longer changes it implicitly.
- Settings submit only the fields that were edited; the "rescan after applying" checkbox became a separate "Rescan with the new settings" button, disabled without a target or during a scan, and applying never starts a scan.
- An open window asked to scan while the scanner is busy now reports the failure instead of recording the new last path and claiming success.

## [QML 0.3.8] - 2026-11-16

### Fixed
- Dock panes have a way out of fullscreen again: the fullscreen layer now carries a slim focus bar with the pane's name on the left and a Restore button (corners-in glyph) on the right; Esc still works. The preset layouts keep their pane headers, so the bar is dock-only.
- The column-menu rail buttons now toggle: a second click closes the menu, another panel's button switches panels, and a click anywhere else closes without acting on what is underneath. The self-test gains a `columns-menu-toggle` check, including real mouse events.
- The buttons of all four dialogs (scan settings, theme settings, scan issues, delete confirmation) moved from a custom `footer` into the content and onto ThemedButton: the footer is not part of the popup's implicit height, so the buttons ended up over or off the frame.
- The dock workspace paints an opaque solidSurface background and its layout geometry is rounded to whole pixels, so on Mica nothing bleeds through the gap between panes and the status bar any more.
- The theme-settings toolbar button shows its label ("Theme settings") next to the paint-brush glyph, matching the scan-settings button.

## [QML 0.3.7] - 2026-11-16

### Fixed
- Controls align with WinShun: a shared ThemedButton (control fill/border/hover/pressed tokens + Phosphor glyph) is used by the scan toolbar buttons; combo boxes and palette.base/button follow the control colours, so they no longer sit translucent on Mica.
- Column visibility is per list: each of the three tables has a fixed rail on its right edge with its own column menu; the menu no longer auto-closes; name/size stay locked. Shared hidden columns in old settings migrate to the file view's own set.
- Dock mode drops the fold/maximize bar under the tab bar (pane headers collapse there); the file view merges its toolbar with the filter bar.
- Expanded folders' left/right colour strips on chart tiles are neutral structure colours, not extra file blocks.
- The maximized tab bar and the hover card use opaque solidSurface, so they no longer show the Mica material through.
- List grid lines are fainter (alpha 0.5 → 0.3).

## [QML 0.3.6] - 2026-11-16

### Fixed
- ComboBox text no longer gets clipped: the box now sizes to its widest entry and the Basic style's own left/right padding is cleared, so the text area is no longer eaten by double padding; the drive popup sizes to the longest row.
- Scroll bars fade out while idle and reappear on hover.
- Dock mode no longer repeats each pane's name in its header; the tab bar and the header title are mutually exclusive.
- Tree view default column widths now fit inside the pane, so the name column is not squeezed on first open; dragging and auto-fit are unchanged.
- The main window enables the Windows 11 Mica/acrylic backdrop when conditions allow (following the host's transparency setting and uiStyle), and falls back to opaque colours under software rendering, older Windows, or high contrast. Popup windows are left alone.

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
