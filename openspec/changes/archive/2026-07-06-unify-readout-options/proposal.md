# Unify Readout Widget Options

## Why

Per-instance widget options grew popup-by-popup: `displayMode` + `color` exist on six readout widgets via **two different editors** (`DisplayModeEditorPopup` for five types, `ScaleWeightEditorPopup` for scale weight), while five other readout widgets (`batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`, `profileName`) expose **no options at all** despite rendering the same icon+value pattern. Meanwhile "which type has which options" is maintained in three hand-synced places — `SettingsNetwork::typeHasOptions()`, the QML editors, and the web editor mirror in `shotserver_layout.cpp` — each carrying keep-in-sync comments. Every new readout widget or option today means another bespoke popup and three more edit sites. This change replaces that accretion with one data-driven capability schema and one editor, making every readout behave identically while deleting code.

## What Changes

- **Introduce a readout capability schema**: a single declaration, defined once, of which option keys each widget type supports (`displayMode`, `color`, `dataMode`, `showRatio`, …). It becomes the source of truth consumed by:
  - `SettingsNetwork::typeHasOptions()` (C++/QML gate) — derived from the schema instead of a hand-kept `QSet`
  - the QML layout editor (which editor sections to show)
  - the web layout editor in `shotserver_layout.cpp` (its `typeHasOptions` mirror and its per-type option forms)
- **One unified readout options editor** (QML) replaces `DisplayModeEditorPopup` and `ScaleWeightEditorPopup`. It renders only the sections the type's capability declaration lists (e.g. scale weight = dataMode + displayMode + showRatio + color; temperature = displayMode + color). Both retired popups are deleted.
- **Extend `color` to all readout widgets**: `batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`, `profileName` gain the existing 6-choice per-instance color override (same shared palette/picker; default preserves today's rendering, including dynamic state coloring where present).
- **Extend `displayMode` (text/icon) to the readouts that have icon assets**: `batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`. `profileName` is color-only (no meaningful icon form) — the schema makes such per-type differences a declaration, not a special case.
- **No stored-format changes**: options continue to persist through the existing item-property mechanism (`setItemProperty` / `getItemProperties`, `/api/layout/item`); existing layouts render identically with no migration.
- Non-readout editors (`CustomEditorPopup`, `SleepEditorPopup`, `ScreensaverEditorPopup`) are out of scope, except that their types are gated through the same schema.

## Capabilities

### New Capabilities

- `layout-readout-capability-schema`: the per-widget-type declaration of supported option keys, defined once and consumed by the C++ has-options gate, the QML options editor, and the web editor — adding an option to a type (or a new readout type) requires touching only the schema plus rendering.

### Modified Capabilities

- `layout-widget-instance-config`: the "Per-instance display mode for readout widgets" requirement extends from 4 types to also cover `waterLevel`/`clock` (already shipped but unspecified there) plus `batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`; the "Single source of truth for configurable widget types" requirement upgrades from a boolean has-options set to the capability schema (which options, not just whether); editor scenarios now route through the single unified readout editor.
- `layout-readout-widget-colors`: the per-instance `color` override extends from 6 readout widgets to all 11 (adds `batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`, `profileName`), same palette, same defaults-preserve-today rule.

## Impact

- **QML**: new unified readout options editor component; `DisplayModeEditorPopup.qml` and `ScaleWeightEditorPopup.qml` deleted; `SettingsLayoutTab.qml` routing simplified; the five newly-optioned item components (`BatteryLevelItem`, `ScaleBatteryItem`, `DoseWeightItem`, `MilkWeightItem`, `ProfileNameItem`) read `color`/`displayMode` from item properties. New/removed QML files must be reflected in `CMakeLists.txt` (`qt_add_qml_module`).
- **C++**: `src/core/settings_network.{h,cpp}` — `typeHasOptions()` reads the schema; schema exposed to QML (invokable returning the type's capability list).
- **Web editor**: `src/network/shotserver_layout.cpp` — mirror replaced by (or generated from) the schema; per-type option forms for the new readouts.
- **Users**: strictly additive capability — existing layouts unchanged; five widgets gain options they previously lacked; the options dialog looks the same for types that already had one.
- **Docs**: `layout-widget-instance-config` and `layout-readout-widget-colors` specs updated via deltas; CLAUDE.md's "4-place widget registration" note gains the schema as the options registration point.
