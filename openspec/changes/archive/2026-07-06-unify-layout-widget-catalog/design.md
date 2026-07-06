# Design: Unify the Layout Widget Catalog

## Context

After #1432, the capability table (type → option keys) lives once in C++ and is injected into the web editor as JSON. The catalog is the remaining mirrored data:

- `qml/pages/settings/LayoutEditorZone.qml` — `catalog` array (type, cat, translated label) + `getItemDisplayName()` chip-name map (~40 entries each)
- `qml/components/library/LibraryItemCard.qml` — a second private `getItemDisplayName()` map
- `src/network/shotserver_layout.cpp` — `WIDGET_TYPES` (type, cat, label, special/screensaver flags, "keep the two in sync" comment) + `DISPLAY_NAMES`

Plus the battery display-mode default hand-coded in four places (`ReadoutOptionsPopup.qml` `defaultDisplayMode()`, `shotserver_layout.cpp` `dispDefault`, `BatteryLevelItem.qml`, `ScaleBatteryItem.qml`), currently held together by cross-link comments added during the #1432 review.

## Goals / Non-Goals

**Goals:**
- One C++ catalog table; QML palette, QML chip names, library card, and web editor all derive from it
- Translation behavior byte-identical: same keys, same fallbacks, live language switch preserved
- Battery display default declared once in the schema; the four hand-coded checks deleted
- Pure consolidation: zero visible behavior change, no storage changes

**Non-Goals:**
- No palette content changes (no widget added/removed/renamed, no category moves)
- No consolidation of the data-mode/color choice lists (see D5)
- No change to `LayoutItemDelegate.qml`'s type→file routing or `LayoutCenterZone.qml` gating — those map types to *implementations*, which is inherently per-surface code, not catalog data

## Decisions

### D1: Catalog is a static C++ table beside the capability schema

A `QVector`/`QHash` of entries `{type, category, labelKey, labelFallback, chipKey, chipFallback, flag}` in `settings_network.cpp`, next to `readoutOptionSchema()`. Same rationale as #1432: C++ is the one place all three consumer surfaces (QML, web-served page, tests) can reach.

*Alternative considered:* a JSON resource file — adds a load/parse step and a second format for no benefit at this size.

### D2: Labels are stored as (translation key, English fallback) pairs

QML consumers call the existing `TranslationManager.translate(key, fallback)` with pairs from the catalog, keeping the current keys (`layoutEditor.widget*` for palette, `layoutEditor.chip*` for chips) so existing translations keep working and language switch stays live (the QML binding re-evaluates on `translationVersion`, as the palette does today). The web editor uses `labelFallback`/`chipFallback` directly — identical to its current English strings.

### D3: QML consumes the catalog via invokables returning QVariantList/QVariantMap

`Q_INVOKABLE static QVariantList widgetCatalog()` (ordered entries for the palette) and `Q_INVOKABLE static QVariantMap widgetChipNames()` (type → {key, fallback}) — or a single catalog call QML reshapes. `LayoutEditorZone.qml` builds its picker model from it (keeping the existing filter/sort/category-header logic); both `getItemDisplayName()` bodies become a lookup + translate. Unknown types fall back to the raw type string, as today.

### D4: Display-mode default extends the schema, not the capabilities JSON shape

Keep `WIDGET_CAPABILITIES` as type→keys (stable, pinned by tests). Add a parallel C++ map (only non-`text` entries: `batteryLevel`, `scaleBattery` → `icon`) exposed as `Q_INVOKABLE static QString defaultDisplayModeForType(type)` and injected into the web page as a small `WIDGET_DISPLAY_DEFAULTS` object. `ReadoutOptionsPopup.defaultDisplayMode()` and the web `dispDefault` become lookups; the two item components bind their default through the invokable.

*Alternative considered:* changing the capabilities JSON to `{keys, defaults}` objects — touches every consumer and the pinned tests for no extra safety.

### D5: Choice lists stay per-surface (evaluated, deferred)

The data-mode and color choice lists are duplicated between `ReadoutOptionsPopup.qml` and the web JS, but: (a) their *values* are already guarded — the schema tests pin the key vocabulary, `WidgetColor` is the QML single source for colors, and an out-of-vocabulary value degrades gracefully; (b) their *labels* legitimately differ per surface (translated vs English); (c) they change far less often than the catalog. Serializing them would add plumbing to delete two short arrays. If a data mode is added and the mirrors bite, revisit — the injection mechanism will be sitting right there.

## Risks / Trade-offs

- [Palette model rebuild in QML could regress filter/sort/headers] → keep the existing model-building JS in `LayoutEditorZone.qml`, only swapping its data source from the inline array to the invokable; verify the picker visually
- [Translation reactivity: a one-time invokable snapshot won't re-translate on language switch] → the catalog returns keys+fallbacks and QML does the `translate()` call inside bindings that read `TranslationManager.translationVersion`, same as today's code
- [Web page grows by the injected catalog JSON] → ~40 compact entries, negligible against the existing inline page
- [Tests pin catalog facts and rot] → pin *invariants*, not the full list: every catalog type is unique, categories are in range, every schema/bespoke type appears in the catalog, chip map covers every catalog type

## Migration Plan

No data migration — catalog contents are identical before and after; saved layouts reference types only. Rollback = revert the PR.

## Open Questions

- None blocking. (If the library-sharing web pages (`shotserver_layout.cpp:4135/4163`) use `DISPLAY_NAMES` beyond the two sites found, they consume the same injected catalog — confirm during implementation.)
