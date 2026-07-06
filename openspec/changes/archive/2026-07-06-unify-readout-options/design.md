# Design: Unify Readout Widget Options

## Context

Per-instance widget options are currently spread across:

- `SettingsNetwork::typeHasOptions()` ([settings_network.cpp:594](../../../src/core/settings_network.cpp)) — a hand-kept `QSet<QString>` of configurable types, with a "keep in sync" comment
- `qml/pages/settings/SettingsLayoutTab.qml` — routes each type to one of five bespoke popups (`DisplayModeEditorPopup`, `ScaleWeightEditorPopup`, `SleepEditorPopup`, `ScreensaverEditorPopup`, `CustomEditorPopup`)
- `src/network/shotserver_layout.cpp` (~line 2596) — a JS mirror of `typeHasOptions` plus hardcoded per-type option forms in the served web editor

`displayMode` + `color` are implemented twice (two popups) and missing from five readouts that render the same label+value pattern (`batteryLevel`, `scaleBattery`, `doseWeight`, `milkWeight`, `profileName`). Storage is uniform already — everything goes through `setItemProperty`/`getItemProperties` and `/api/layout/item` — so this is purely an editor/gate/declaration unification; no data migration.

## Goals / Non-Goals

**Goals:**
- One declaration of "type → supported option keys," consumed by C++, QML, and the web editor
- One QML options editor for all readout types; delete `DisplayModeEditorPopup.qml` and `ScaleWeightEditorPopup.qml`
- `color` on all 11 readouts; `displayMode` on the 10 with icon forms
- Existing layouts render pixel-identical with no stored-property changes

**Non-Goals:**
- No changes to bespoke editors (`custom`, `sleep`, screensavers/`shotPlan`) beyond gating them through the same schema
- No new option kinds, no palette changes, no zone-option changes
- No storage-format or web-API changes

## Decisions

### D1: Schema lives in C++ as a static table in `settings_network.cpp`

A static `QHash<QString, QStringList>` (type → option keys) next to `typeHasOptions()`, which becomes a lookup into it (bespoke-editor types are listed with a sentinel/bespoke marker so gating stays schema-driven). Exposed to QML via a `Q_INVOKABLE QStringList optionKeysForType(const QString&)`.

*Why C++ over a QML/JS singleton:* `typeHasOptions()` is already C++ and static; the web editor is served from C++, so C++ is the one place all three consumers can reach without duplication.

*Alternative considered:* a JSON resource file loaded by all three — more indirection for no benefit at this size.

### D2: Web editor consumes the schema as injected JSON, not a mirror

`shotserver_layout.cpp` serializes the same static table into the served page (a `const WIDGET_CAPABILITIES = {...}` blob, or a small `/api/layout/capabilities` endpoint). The JS `typeHasOptions` mirror and its sync comment are deleted; the web option forms branch on capability keys the same way the QML editor does.

*Why injection over endpoint:* the layout page is already assembled server-side; embedding avoids a fetch round-trip and a new API surface. If the page is served as a static resource instead, fall back to the endpoint.

### D3: One `ReadoutOptionsPopup.qml` with capability-gated sections

Sections in fixed order: data mode (scale weight only), display mode, ratio suffix (scale weight only), color (shared `WidgetColorPicker`). Each section's visibility = `optionKeys.includes(key)`. `SettingsLayoutTab.qml` routes every readout type here; the bespoke popups keep their existing routing.

*Why not fold bespoke editors in too:* `custom` and `shotPlan` are structurally different UIs (WYSIWYG, chip reorder); forcing them into a section model would grow the unified editor back into a special-case pile. They stay out of scope per the proposal.

### D4: `displayMode` semantics — "unset = today's rendering," per type

For the six types that already have the mode, unset = `text` (unchanged). For `batteryLevel`/`scaleBattery`, whose current rendering already leads with an icon, unset = today's icon+value form, and the mode offers the value-only form. `doseWeight`/`milkWeight` get unset = current text form, `icon` renders an icon ahead of the value. `profileName` is color-only in the schema.

*Why:* the invariant that matters is "existing layouts don't change," not "text is always the default."

### D5: Icon assets reuse existing SVGs

`doseWeight` → the beans icon already used by the Beans widget; `milkWeight` → the steam/pitcher icon used by steam UI. No new assets; if an asset proves visually wrong, that's a follow-up polish decision, not schema work. (Verify exact `qrc:/icons/` paths during implementation.)

## Risks / Trade-offs

- [Web editor regression — its option forms are hand-rolled JS] → keep the form-rendering change mechanical (branch on capability keys), and manually exercise each readout type in the web editor before merge
- [Subtle rendering drift on the six already-optioned types when swapping popups] → the popup writes the same property keys/values as before; verify by diffing stored item JSON before/after editing in the new popup
- [Battery widgets' "inverted" default could confuse the editor UI] → label the mode choices by what they show ("Icon + value" / "Value only") rather than abstract text/icon terms
- [`tests/` may stub `typeHasOptions` or shotserver layout endpoints] → build `--target all` with `-DBUILD_TESTS=ON` before pushing (known MCP-stub gotcha applies to server files)

## Migration Plan

No data migration: option keys, values, and storage mechanism are unchanged; new options are absent-by-default and absent means today's rendering. Rollback = revert the PR; stored properties written by the unified editor are readable by the old popups (same keys).

## Open Questions

- Should `ratioQuickSelect` (a readout-adjacent interactive widget) get `color` too? Deferred — not in the modified spec; trivial to add later via one schema line.
- Whether the CLAUDE.md "new layout widgets: 4 places" checklist should be updated in this PR (it gains "add schema entry") — yes, cheap, do it in the docs task.
