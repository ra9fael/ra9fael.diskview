# Disk View QML (ra9fael.diskview.qml)

[简体中文](README.md) · **English**

A QML disk-space viewer for WinShun. Explore a treemap or sunburst, hover for full paths, click to keep selection details, and double-click folders to navigate. The tree stays size-descending while the file list sorts independently. Right-click provides path, file, Explorer, properties, Recycle Bin, and permanent-delete actions. Whole fixed NTFS drives offer an optional elevated fast scanner; other paths use standard enumeration. Typing a query never starts a scan.

## Install and update

Obtain **ra9fael.diskview.qml-0.3.6.wsplugin** from the repository release or local dist/. Verify the adjacent .sha256 with PowerShell's Get-FileHash if desired. In WinShun, open Settings → Plugins → Installed → Add plugin → Archive. Enable the locally installed plugin and review its permissions. Installing the same id updates it after the host's replacement prompt and preserves the plugin data directory.

Version 0.2.0 adds `files:write` for Recycle Bin and permanent-delete actions. WinShun asks you to confirm this permission during upgrade.

Version 0.3.1 improves light-theme contrast, percentage bars, column resizing, drive selection, address suggestions, and chart navigation. Version 0.3.2 restores WinShun surface roles, allows splitting a fully merged Dock group, and keeps table width fixed while resizing adjacent columns. Version 0.3.3 shows Dock direction controls, scopes filtering to the file view, and highlights chart items by extension. Version 0.3.4 permits scan-root actions, fixes narrow-Dock and Extension View resizing, and closes the column menu on pointer exit. Version 0.3.5 switches back to a drive ComboBox and aligns the fallback palette with WinShun's default theme. Version 0.3.6 fixes clipped ComboBox text, idle-fading scroll bars, duplicate Dock pane titles and tree column widths, and enables a Mica backdrop where supported.

Version 0.2.0 adds **processes** for a short-lived scan helper. Windows administrator consent is separate. Uninstall through the host's plugin list. This native executable is outside the script-only community index.

## Use

| Action | Behavior |
| --- | --- |
| disk | Lists fixed-drive capacities; choose a drive to open and scan, or open the welcome screen |
| disk D:\work | Opens and scans that directory |
| Click a tile, slice or row | Selects the item, keeps details visible, and highlights it in both panes |
| Double-click a folder / Enter / Enter folder | Navigates into it; files are never launched |
| Hover | Shows the full path in the status bar immediately and a tooltip after about 250 ms |
| Breadcrumbs | Navigate to ancestors; long paths retain root, parent and current folder |
| Back / Forward / Up | Browse history or navigate to the parent |
| Ctrl+L | Edits the address; Enter locates an indexed folder, while other paths get an explicit Scan path action |
| Current / Largest | Lists immediate children or up to 1000 largest files in the current subtree |
| Filter / column headings | Searches the current list and sorts by name or size; Largest filters only that ranking |
| Locate parent | Opens a selected file's parent while keeping it selected |
| Tiles / Rings / Depth | Defaults to one level, optionally two; small items aggregate without redistributing their size |
| Drag divider | Resizes panes; narrow windows offer Chart / List tabs |
| Scan issues / Logical size | Explains coverage, read errors, unknown sizes, unattached records, skipped links and accounting |

Keyboard: Alt+Left / Right for history; Backspace or Alt+Up for parent; F6 / Shift+F6 to switch focus between chart and list; Ctrl+F for filtering; Ctrl+K for command search; Ctrl+C to copy the full path; Ctrl+Shift+C to copy the item; Ctrl+Shift+X to cut it; arrows to pick the nearest item by screen position in the chart and to move through the list; Tab / Shift+Tab for controls; Enter / Space for focused buttons; + / - / 0 to zoom or fit; Shift+F10 for the item menu; Delete for the Recycle Bin confirmation; permanent delete has no shortcut (pick it in the item menu and confirm by typing the name); F1 for all shortcuts; F5 for scan settings. Text inputs retain editing behavior. Esc closes a tool window, exits address editing or clears filtering; Alt+F4 closes the window.

The window follows host colors and offers a light/dark switch. The defaultView setting selects the initial chart. Scan settings (scan mode, worker limit, media strategy, preview interval, next-scan exclusions) are stored beside the last scanned path in the plugin data directory and restored on the next start.

## Scanning and results

- A whole fixed NTFS drive uses the scan mode chosen in scan settings. Automatic uses the fast engine for a whole NTFS volume; it can also be pinned to fast or standard. Only the separate fast helper requests elevation; the UI stays unelevated. Declined consent or helper failure automatically falls back to standard scanning, with a visible reason.
- Folders, other filesystems and UNC paths use directory enumeration, including extended-length paths. Directory reparse points are not followed. Standard scanning uses a bounded directory worker pool whose limit is set in scan settings; more workers are not promised to be faster.
- During a scan, progress shows discovered items, bytes, time and path without inventing an ETA or completion percentage. It can be paused and resumed. Browse the previous result or explicitly switch to the current partial result.
- Cancellation or failure restores the previous complete result when available. A first scan may keep discovered entries, explicitly marked incomplete. Scan issues includes the failure reason.
- Snapshots exist only in the current window's memory. Persistent scan caches and USN incremental updates are not implemented.

The fast engine reads MFT names and unnamed $DATA logical sizes, not file contents or alternate data stream contents. Hard links are counted per visible path. Unknown sizes remain unknown. A scan is not a transactional volume snapshot: concurrent changes can cause differences or omissions. Filesystem metadata, named streams and unreadable areas are not guaranteed to be fully represented.

Charts show logical bytes using base 1024. Cluster-rounded allocation is only an estimate, especially for sparse, compressed and hard-linked files. File totals are not the volume's used-space value. Partial-result shares use the entries discovered so far.

## Permissions and data

| Permission | Purpose |
| --- | --- |
| ui | Native Win32 / GDI window |
| files:read | Directory and filesystem metadata |
| files:write | Moving a selected item to the Recycle Bin, or deleting it permanently after a two-step confirmation |
| clipboard:write | Putting the selected item on the clipboard as CF_HDROP (copy or cut) for Explorer to paste |
| processes | Same-executable scan helper using a restricted local metadata pipe |

Scanning itself is read-only: no file content reads, networking, services or startup registration. There are exactly two writes, both picked explicitly in the item menu and confirmed: handing the item to the system Recycle Bin through IFileOperation, and "Delete permanently", which bypasses the Recycle Bin, requires reading a warning and typing the item's name, and cannot be undone. Protected system paths and their descendants are not offered; a failure keeps the item and shows the reason. Copying or cutting an item only writes its path to the clipboard as CF_HDROP (with the Preferred DropEffect); Explorer performs the actual paste. The plugin writes the last scanned path and the scan settings to diskview.json in the host's plugin data directory. Trees are released on window close. The elevated helper does not stay resident after its work or connection ends.

## Build and preview

Windows 10/11 x64, CMake 3.21+ and an MSVC or MinGW-w64 compiler with C++20 support. Use a Developer PowerShell for MSVC. Packaging and protocol tests require Python 3.10+.

~~~powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python tools/package.py
Start-Process .\diskview.exe -ArgumentList '--view' -WindowStyle Hidden
~~~

diskview.exe --view "D:\work" opens a standalone viewer and scans the given folder. Without a path it opens the welcome screen. Preview mode saves no plugin configuration. Starting without arguments retains the host's JSON-RPC protocol mode.

Tests exercise scan totals, cancellation, immutable partial snapshots, MFT parsing, hard links, unknown sizes, reference sequence numbers, extension and corrupt records, and pipe packet bounds. Hidden-window tests cover selection, navigation, filtering, keyboard controls, layout and result recovery. The real-process protocol test checks Unicode paths, appearance updates, reopening and shutdown.

Run tools/verify/render-views.ps1 to render native UI snapshots in build/verify/. Development verification scripts are excluded from the package. See [implementation-status.md](design/implementation-status.md) for implementation status and local measurements.

## License

[MIT](LICENSE). The reused WinShun MFT reader and its MIT license are in [vendor/winshun](vendor/winshun/README.md); its license is included in the distribution.
