# Disk View (`ra9fael.diskview`)

> [简体中文（默认）](README.md) · **English**
>
> Win顺 defaults to a Chinese UI but ships an English mode too, so the Chinese
> guide lives in `README.md` and the English guide here; the two stay in sync
> and link to each other.

A disk-usage visualizer for Win顺: scan any folder and see where the space went
as a **recursive treemap** (default) or a **recursive sunburst**. Area is size —
bigger tile, more space. Hover for details, click to drill down, and a
"Largest files" list stays docked on the right. Queries never scan: scanning
happens only inside the window, when you trigger it. The plugin never touches
the file system while you type.

## Install

1. Download `ra9fael.diskview-<version>.wsplugin` from the
   [latest Release](https://github.com/ra9fael/winshun-diskview/releases/latest).
   Optionally verify the SHA-256 with the adjacent `.sha256` file
   (`Get-FileHash <file> -Algorithm SHA256`).
2. In Win顺, open **Settings → Plugins → Installed → Add plugin → Archive
   (.zip / .wsplugin)** and pick the file.
3. Plugins installed from a local archive are **not enabled automatically**.
   Turn the switch on in the list; enabling is how you confirm the permissions
   it asks for (see [Permissions and data](#permissions-and-data)).

**Update:** install the new `.wsplugin` the same way. Win顺 recognizes the id,
asks before replacing the old version, and keeps the plugin's data directory.

**Uninstall:** remove it under **Settings → Plugins → Installed**.

This is a native exe, so it does not appear in Win顺's in-app plugin index
(the community index only lists script plugins). This repository's Releases
are the only distribution channel.

## Usage

Type `disk` in the Win顺 box and press Enter to open the window:

| Input | Behavior |
| --- | --- |
| `disk` | Opens the window. First run shows a welcome page; later runs remember the last scanned path |
| `disk D:\work` | Opens the window and **immediately** scans `D:\work` (the only scan entry point, always user-triggered) |

In the window:

- **Path box + Scan**: type any folder path and press Enter or click "Scan";
  while scanning the button becomes "Stop" and clicking cancels. A cancelled
  scan keeps the previous complete result — no half-finished trees.
- **Treemap / Sunburst**: switch anytime (Ctrl+V); the startup view is the
  `defaultView` setting.
- **Drill / Back**: click a tile or slice to enter that folder; "Back" or
  Backspace goes up; click a breadcrumb in the status bar to jump to any
  ancestor.
- **Largest files**: the right-hand list shows the biggest files in the
  current range (up to 64); clicking a row jumps to its folder. Scroll with
  the wheel.
- **Hover**: any tile / slice shows kind, size, file and folder counts, and
  unreadable-entry count; files also get a cluster-aligned allocation
  estimate.
- **Esc** closes the window; **F5** rescans the current path.

## The two views

- **Recursive treemap (default)**: a squarified treemap. Folders nest inward,
  files are leaf tiles, colors group by extension category (video / archive /
  image / documents…), stable across frames and rescans. Entries too small to
  draw (below 24 dpi-scaled pixels) are omitted — their space stays as
  background gaps and the parent tile keeps its true total; nothing is
  redistributed.
- **Recursive sunburst**: concentric rings, inner folders outward to their
  children, angle proportional to share. Slices below 0.003 radians are
  omitted — again leaving gaps, never changing totals.

## Build from source

Requirements: Windows 10/11 x64, CMake 3.21+, and Visual Studio 2022 or newer
(MSVC) or MinGW-w64 GCC 13+. Packaging and integration tests need Python 3.10+.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # MSVC; MinGW: -G "MinGW Makefiles"
cmake --build build                                       # produces diskview.exe next to plugin.json
ctest --test-dir build --output-on-failure                # pure-logic tests (layout / scan / path normalization)
python tools/package.py                                   # builds dist/<id>-<version>.wsplugin
```

The logic tests cover treemap area conservation and proportions, the gaps left
by culled entries, sunburst angular continuity and nested radii, scan
aggregation on a real temporary directory (including the largest-files order),
cancel semantics, invalid-path errors, and `\\?\` long-path / UNC prefix
normalization.

## Permissions and data

| Permission | Why |
| --- | --- |
| `ui` | Opens the standalone scan window (Win32 + GDI, no third-party dependencies) |
| `files:read` | Enumerates directory and file metadata while scanning (FindFirstFile); read-only, never opens file contents |

- **No** other permissions: no admin elevation, network, clipboard, or
  autostart.
- Scanning reads metadata only: sizes, timestamps, tree structure. File
  contents are never read and never uploaded — the plugin has no networking
  code at all.
- **Data location**: the last scanned path is stored as `diskview.json` (a few
  dozen bytes) in the data directory Win顺 assigns to each plugin. Nothing else
  is written; the registry is untouched.
- Uninstalling removes that data directory along with the plugin's other data.

## Appearance

The window paints itself with the host's own colours (`appearance.nativeLook`:
background, text, accent, the accent-filled button and the text on it), so it
looks like the same interface as Win顺; when the host switches theme or
interface style, an open window follows. A host too old to send a look gets
the plugin's built-in light and dark palettes. The startup view is the
`defaultView` setting (`treemap` or `sunburst`); switch it in the window any
time with Ctrl+V.

## Failure and edge handling

- Missing path / not a folder: the content area shows an error; no stale data
  is displayed.
- Unreadable directories (system ACLs etc.): counted on the parent node as
  "unreadable entries" and listed in the status bar (up to 32 paths); the scan
  itself continues.
- Symlinks / reparse points: counted but **never followed**, avoiding loops
  and double counting.
- Cancelled scan: the partial tree is discarded, the previous complete result
  stays, and the status bar notes the cancellation.
- Long paths (>260 chars) work via the `\\?\` prefix; UNC network paths scan
  too.

## License

[MIT](LICENSE)
