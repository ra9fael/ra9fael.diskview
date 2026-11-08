# Changelog

[English](CHANGELOG.en.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

## [0.2.0] - 2026-11-09

- 修复可执行文件图标，补齐打包中的 ICO 和资源脚本。 为窗口显式设置大小图标，随 DPI 更新并释放图标句柄。 同时包含当前工作区的 0.2.0 扫描和界面改动；新增 processes 权限，升级需要确认。
- 本次仅生成本地标签与安装包。实际验证范围见 docs/icon-validation.md；未进行 GitHub 发布。更新后需重新启动插件进程。

### Added
- Optional read-only MFT scanning for whole fixed NTFS drives, using a
  short-lived elevated helper and a restricted local metadata pipe. The UI
  stays unelevated; declined consent or helper failure falls back to standard
  enumeration with a visible reason. Adds the `processes` permission.
- Top breadcrumbs, separate back/forward history and parent navigation,
  editable indexed paths with Ctrl+L, persistent single-click selection and
  double-click folder navigation.
- Delayed full-path hover overlays, matching status text, synchronized chart
  and list highlighting, selection details and a locate-parent action.
- One/two-level layouts, small-item aggregation, resizable panes, narrow-window
  chart/list tabs, filtering, sorting, pagination and up to 1000 largest files.
- Nonmodal scan progress, immutable partial previews and previous-result
  recovery; incomplete results, unknown sizes and failures stay explicit.
- Keyboard focus navigation, a standalone `--view [path]` preview mode,
  updated light/dark styling, executable icon and version resources.
- MFT/parser/transport and native interaction regressions; the distribution
  includes the vendored WinShun reader's MIT license and all required sources.
- Fixed-drive capacity and usage in query results and clickable welcome cards.
- A Folders tab with immediate subfolder sizes and shares, including empty
  folders; the largest-files ranking now follows the current folder.
- Clickable skipped-directory details in the status bar.
- Window-message regression tests, deterministic visual snapshots, and a
  real-process protocol smoke test for scan, reopen, theme updates and shutdown.

### Fixed
- Drive-root and extended-path normalization, relative paths and forward slashes.
- Navigation through nested tiles, root-level file rows, breadcrumbs and Up.
- Failed scans expose the error while retaining an explicitly identified
  previous result; cancellation preserves complete or labelled partial data.
- Sunburst rings stay inside the chart at small sizes and high DPI; status
  text no longer overlaps breadcrumbs or totals.
- The path box follows drive-card scans; F5 also works from the path box.
- Reopening clears previous errors and cancellation state; shutdown destroys
  pending scan messages using their correct types.


## [0.1.0] - 2026-11-08

### Added
- Initial release. `disk` opens the disk view window; `disk <path>` opens and
  scans that folder immediately. Scanning is always user-triggered and only
  happens in the window.
- Recursive treemap (squarified, default) and recursive sunburst, switchable
  in the window (Ctrl+V) and via the `defaultView` setting. Culled entries
  leave honest gaps; parent totals are never redistributed.
- Drill down by clicking tiles / slices, go up with Backspace or the Back
  button, jump via status-bar breadcrumbs.
- "Largest files" list (up to 64 rows) with click-to-folder navigation.
- Hover details: kind, size, file/folder counts, unreadable entries, and a
  cluster-aligned allocation estimate for files.
- Scanner: cancellation with previous-result retention, per-node unreadable
  accounting (up to 32 paths listed), reparse points counted but never
  followed, `\\?\` long-path and UNC support, logical vs cluster-rounded
  sizes.
- The window paints itself with the host's own colours (`appearance.nativeLook`),
  falling back to built-in light and dark palettes; it follows a theme or style
  change while it is open.
- The `defaultView` setting decides the startup view; the saved config keeps
  only the last scanned path (`diskview.json` in the plugin data directory).
- Per-monitor V2 DPI awareness.
