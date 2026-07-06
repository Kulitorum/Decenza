# Tasks: Unify the Layout Widget Catalog

## 1. C++ catalog + schema default (source of truth)

- [x] 1.1 Add the widget catalog table in `src/core/settings_network.cpp` (type, category, palette labelKey+fallback, chip labelKey+fallback, special/screensaver flag) — entries transcribed 1:1 from `LayoutEditorZone.qml`'s `catalog` + `getItemDisplayName()` and cross-checked against the web `WIDGET_TYPES`/`DISPLAY_NAMES`
- [x] 1.2 Expose `Q_INVOKABLE static QVariantList widgetCatalog()` and a chip-name lookup to QML; add a JSON serializer for the web page
- [x] 1.3 Add the display-mode default map (`batteryLevel`/`scaleBattery` → `icon`) with `Q_INVOKABLE static QString defaultDisplayModeForType(const QString&)` and include it in the web injection as `WIDGET_DISPLAY_DEFAULTS`
- [x] 1.4 Tests in `tests/tst_settings.cpp`: catalog invariants (unique types, valid categories, every configurable type present, chip coverage), default-mode pinning (`icon` for the two battery types, `text` otherwise), and JSON parity for both injected blobs

## 2. QML consumers

- [x] 2.1 `LayoutEditorZone.qml`: build the add-widget picker model from `widgetCatalog()` (keep existing filter/sort/category-header logic and `translationVersion` reactivity); replace `getItemDisplayName()` body with a catalog lookup
- [x] 2.2 `LibraryItemCard.qml`: delete its private `getItemDisplayName()` map, use the shared lookup
- [x] 2.3 `ReadoutOptionsPopup.qml`: `defaultDisplayMode()` delegates to `Settings.network.defaultDisplayModeForType()`
- [x] 2.4 `BatteryLevelItem.qml` + `ScaleBatteryItem.qml`: displayMode default reads the schema invokable instead of the hardcoded "icon"; remove the now-redundant cross-link comments

## 3. Web editor

- [x] 3.1 Inject the catalog JSON into the served layout page; delete `WIDGET_TYPES`, `DISPLAY_NAMES`, `CAT_NAMES` duplication where applicable, and the "keep the two in sync" comment; adapt `renderZones()`/add-menu/library pages (`shotserver_layout.cpp:4135/4163`) to the injected data
- [x] 3.2 Replace the JS `dispDefault` battery check with a `WIDGET_DISPLAY_DEFAULTS` lookup
- [ ] 3.3 Verify the web palette renders identically (categories, order, labels, special/screensaver coloring) and chip names match

## 4. Docs, verification, wrap-up

- [x] 4.1 Update CLAUDE.md's widget-registration note: catalog entry replaces the `LayoutEditorZone.qml` and `shotserver_layout.cpp` list edits (registration becomes: CMakeLists + `LayoutItemDelegate.qml` switch + catalog entry, plus schema entry if it has options)
- [x] 4.2 Full build + test suite green
- [ ] 4.3 Jeff verifies in-app: add-widget picker (grouping, labels, filter), chip names, library card names, battery widgets' default rendering and options popup; plus the web editor palette round-trip
