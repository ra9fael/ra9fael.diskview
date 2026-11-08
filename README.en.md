# Disk View (ra9fael.diskview)

[简体中文](README.md) · **English**

A native disk-space viewer for WinShun. Explore a treemap or sunburst, hover for full paths, click to keep selection details, and double-click folders to navigate. Whole fixed NTFS drives offer an optional elevated fast scanner; other paths use standard enumeration. Typing a query never starts a scan.

## Install and update

Obtain **ra9fael.diskview-0.2.0.wsplugin** from the repository release or local dist/. Verify the adjacent .sha256 with PowerShell's Get-FileHash if desired. In WinShun, open Settings → Plugins → Installed → Add plugin → Archive. Enable the locally installed plugin and review its permissions. Installing the same id updates it after the host's replacement prompt and preserves the plugin data directory.

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

Keyboard: Alt+Left / Right for history; Backspace or Alt+Up for parent; Ctrl+F for filtering; arrows for list selection; Tab / Shift+Tab for controls; Enter / Space for focused buttons; F5 to rescan the scan root. Text inputs retain editing behavior. Esc exits address editing, clears filtering or selection; Alt+F4 closes the window.

The window follows host colors and offers a light/dark switch. The defaultView setting selects the initial chart.

## Scanning and results

- A whole fixed NTFS drive offers fast or standard scanning. Only the separate fast helper requests elevation; the UI stays unelevated. Declined consent or helper failure automatically falls back to standard scanning, with a visible reason.
- Folders, other filesystems and UNC paths use directory enumeration, including extended-length paths. Directory reparse points are not followed.
- During a scan, progress shows discovered items, bytes, time and path without inventing an ETA or completion percentage. Browse the previous result or explicitly switch to the current partial result.
- Cancellation or failure restores the previous complete result when available. A first scan may keep discovered entries, explicitly marked incomplete. Scan issues includes the failure reason.
- Snapshots exist only in the current window's memory. Persistent scan caches and USN incremental updates are not implemented.

The fast engine reads MFT names and unnamed $DATA logical sizes, not file contents or alternate data stream contents. Hard links are counted per visible path. Unknown sizes remain unknown. A scan is not a transactional volume snapshot: concurrent changes can cause differences or omissions. Filesystem metadata, named streams and unreadable areas are not guaranteed to be fully represented.

Charts show logical bytes using base 1024. Cluster-rounded allocation is only an estimate, especially for sparse, compressed and hard-linked files. File totals are not the volume's used-space value. Partial-result shares use the entries discovered so far.

## Permissions and data

| Permission | Purpose |
| --- | --- |
| ui | Native Win32 / GDI window |
| files:read | Directory and filesystem metadata |
| processes | Same-executable scan helper using a restricted local metadata pipe |

Scanning is read-only: no file deletion, moving, content reads, networking, services or startup registration. The only saved plugin data is the last successful path in diskview.json in the host's plugin data directory. Trees are released on window close. The elevated helper does not stay resident after its work or connection ends.

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
