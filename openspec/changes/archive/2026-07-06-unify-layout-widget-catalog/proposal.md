# Unify the Layout Widget Catalog

## Why

PR #1432 unified *which options each widget type has* behind one C++ table, but the widget **catalog** itself — the list of types, their categories, palette labels, and short chip names — is still hand-synced across three places: the in-app palette in `LayoutEditorZone.qml` (`catalog` array + `getItemDisplayName()` map), a second QML copy in `LibraryItemCard.qml` (`getItemDisplayName()`), and the web editor's `WIDGET_TYPES` + `DISPLAY_NAMES` in `shotserver_layout.cpp` (with its own "keep the two in sync" comment). Separately, the per-type default display mode ("icon" for the battery readouts, "text" otherwise) is duplicated as hand-coded logic in four places, held together only by cross-link comments. Every new widget today means three catalog edits; every new icon-default readout means four default edits. This change finishes the mirror-kill that #1432 started, using the same proven mechanism.

## What Changes

- **Single C++ widget catalog**: one table (in `settings_network.cpp`, beside the capability schema) declaring, per widget type: category, palette-label translation key + English fallback, chip-label translation key + English fallback, and the special/screensaver display flag. Consumed by:
  - the in-app add-widget palette and chip labels (`LayoutEditorZone.qml`) via a new invokable — QML keeps translating through `TranslationManager.translate(key, fallback)`
  - `LibraryItemCard.qml`'s display-name lookup (its private map is deleted)
  - the web layout editor, which receives the catalog as injected JSON (same mechanism as `WIDGET_CAPABILITIES`) — `WIDGET_TYPES`, `DISPLAY_NAMES`, and their keep-in-sync comments are deleted; web labels use the English fallbacks as they do today
- **Per-type display-mode default moves into the capability schema**: a new `defaultDisplayModeForType()` invokable (and a small injected map for the web) replaces the four hand-coded `batteryLevel`/`scaleBattery` checks in `ReadoutOptionsPopup.qml`, `shotserver_layout.cpp`, `BatteryLevelItem.qml`, and `ScaleBatteryItem.qml`.
- **Choice lists (data modes, colors) stay per-surface** — evaluated and deliberately deferred: their values are already pinned by the schema tests' key vocabulary and the labels legitimately differ per surface (translated in QML, English on the web). Consolidating them would add plumbing without deleting a real sync hazard. Documented in design.md.
- **No behavior changes**: same palette contents, same labels, same category grouping, same defaults. Existing layouts unaffected; no storage changes.

## Capabilities

### New Capabilities

- `layout-widget-catalog`: the single C++ declaration of the widget palette (types, categories, labels, display flags), consumed by the in-app editor, the library card, and the web editor — adding a widget type updates every catalog surface from one edit.

### Modified Capabilities

- `layout-readout-capability-schema`: the schema additionally declares each type's default display mode; all four consumers (unified editor, web editor, item components) derive the default from it instead of hand-coding the battery rule.

## Impact

- **C++**: `src/core/settings_network.{h,cpp}` — catalog table + invokables (`widgetCatalog()`-style accessor for QML, JSON serializer for the web, `defaultDisplayModeForType()`); tests extended in `tests/tst_settings.cpp` (catalog↔schema agreement, default-mode pinning).
- **QML**: `LayoutEditorZone.qml` palette/chip labels read the catalog; `LibraryItemCard.qml` private map deleted; `ReadoutOptionsPopup.qml`, `BatteryLevelItem.qml`, `ScaleBatteryItem.qml` read the default from the schema.
- **Web**: `src/network/shotserver_layout.cpp` — catalog + defaults injected; `WIDGET_TYPES`/`DISPLAY_NAMES`/`dispDefault` mirrors deleted.
- **Docs**: CLAUDE.md's "4 places" widget-registration note shrinks — the catalog entry replaces the `LayoutEditorZone.qml` and `shotserver_layout.cpp` list edits.
- **Users**: none visible — pure consolidation.
