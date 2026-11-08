# Changelog

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

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
- Window preferences (last path, view choice) persisted as `diskview.json`
  in the plugin data directory.
- Dark and light themes taken from the host at window-open time; per-monitor
  V2 DPI awareness.
