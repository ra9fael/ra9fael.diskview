# DiskView QML: settings synchronization and folder actions plan

[中文](sdk-upgrade-plan.md)

Scope: the QML plugin `ra9fael.diskview.qml`, whose shipping manifest is `qmlapp/plugin.json`.

## Why change

`qmlapp/src/plugin_main.cpp::settingsChanged()` currently ignores host settings, and window creation explicitly leaves the host bridge disabled. Consequently the host card and the scanner's local settings can disagree. Some existing documentation describes synchronization that the current entry point does not perform.

The existing scan settings dialog already edits advanced options and exclusion paths. It can become the dedicated configuration entry. A folder-result action is also a natural way to supply an exact scan target without retyping its path.

## Settings and interface split

| Surface | Contents |
|---|---|
| Search | Open viewer, choose a path, recent target and settings entry |
| Host card | Default view, scan mode and folder-menu visibility switch |
| Settings window | Common settings plus workers, media strategy, preview rate, small-file aggregation threshold and excluded paths |

Default view, scan mode and the menu switch are host-owned. Advanced scan parameters are plugin-owned after value preservation and manifest removal. Last path, dock layout, columns, visual state and exclusions remain local.

Keep discrete worker choices `auto / 1 / 2 / 4 / 8`. A continuous worker slider would expose values the scanner does not offer.

## Implementation

1. Update the SDK. Apply host settings at initialization, retain them for later-created windows and forward changes to an open window on its UI thread.
2. Make common controls in the settings UI editable. Remove the obsolete one-way ownership behavior that hides controls to avoid divergence; send only changed host-owned values outward.
3. Preserve existing local advanced scan values and transfer previously host-configured advanced values before removing manifest declarations. Use one canonical enum mapping for host strings and local numeric values.
4. Distinguish default view from the view currently selected in an existing workspace. Changing the default affects the next opening; normal workspace navigation does not silently rewrite the default. Offer an explicit default-view control.
5. Add `settingsWindow: true` and `showSettings()`. Reuse the existing settings content in a top-level settings window so configuration does not require constructing the full viewer or starting a scan. Viewer toolbar and search Settings open the same content.
6. Settings drafts support Apply/Cancel. Apply updates only edited fields; Cancel changes nothing. Changes affect the next scan. Rescan is a separate explicit option and is unavailable when no target exists.
7. Add a `files` trigger with `kind: folder`, `max: 1`, a localized Analyze disk usage title and an `if` setting. Handle its path array in `action()` and start the existing scan path only after that explicit action.
8. Preserve existing elevation and fast-scan behavior. The menu entry does not expand permission requirements or silently choose a different target.

## Acceptance

- Host scan-mode edits reach the open viewer and the next scan.
- Settings-window edits of common settings reach the host; advanced values persist locally.
- Old workers, media and preview selections survive the change in ownership.
- Opening Settings alone does not construct a scan or read a disk tree.
- Cancel leaves all settings unchanged; Apply does not start a scan unless explicit Rescan was selected.
- The folder menu appears only when enabled and eligible, accepts one folder, and scans that exact folder, including paths with spaces and Chinese characters.
- Viewer reopen and standalone mode retain their existing configuration behavior.
- Relevant QML settings, scan behavior and plugin protocol checks pass against isolated data and small fixtures.

## Documentation

Update the QML README and usage docs to match actual synchronization, default-view semantics, settings ownership, folder-result action and next-scan application rules.
