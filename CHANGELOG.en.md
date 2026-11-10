# Changelog

[中文](CHANGELOG.md)

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
- Parent navigation in three places: a titled "↑ Parent" button beside the
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
