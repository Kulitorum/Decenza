# layout-widget-catalog Specification

## Purpose
The single source of truth for the layout widget catalog — each placeable widget type's palette category, palette label, chip label, and display flag — declared once in a C++ table and consumed by the in-app add-widget palette, chip labels, library card display names, and the web layout editor, replacing the previously hand-synchronized copies in QML and JS.
## Requirements
### Requirement: The widget catalog is declared in one place

The layout widget catalog — the set of placeable widget types with, per type: its palette category (Actions, Readouts, Utility, Screensavers), its palette label (translation key + English fallback), its short chip label (translation key + English fallback), and its display flag (`special` / `screensaver` chip coloring) — SHALL be declared in a single C++ table. Adding, renaming, or removing a widget type in the catalog SHALL require no per-surface list edits beyond that table (rendering code for a genuinely new widget is still separate).

#### Scenario: One edit updates every catalog surface

- **WHEN** a widget type's entry is added to or changed in the catalog table
- **THEN** the in-app add-widget palette, the in-app chip labels, the library card display names, and the web editor's palette and chip names all reflect it without any of those surfaces being edited

### Requirement: All catalog consumers derive from the single table

The in-app layout editor's add-widget palette and chip display names (`LayoutEditorZone.qml`), the widget-library card's display-name lookup (`LibraryItemCard.qml`), and the web layout editor's palette and display names SHALL all consume the single catalog. The hand-maintained copies — the QML `catalog` array, both QML `getItemDisplayName()` maps, and the web `WIDGET_TYPES` / `DISPLAY_NAMES` objects — SHALL be removed, along with their keep-in-sync comments.

#### Scenario: In-app palette matches the catalog

- **WHEN** the add-widget picker opens in the in-app layout editor
- **THEN** it lists exactly the catalog's types, grouped by the catalog's categories, labeled with the translated palette labels (English fallback when untranslated)

#### Scenario: Web editor matches the catalog

- **WHEN** the web layout editor page is served
- **THEN** its palette and chip names come from catalog data injected into the page (same mechanism as the capability schema), using the catalog's English fallbacks

#### Scenario: No hand-synced catalog copies remain

- **WHEN** the catalog changes in C++
- **THEN** no other code location must be edited for the in-app editor, library card, and web editor to stay consistent

### Requirement: QML labels remain translatable

Catalog labels SHALL be stored as translation key + English fallback pairs. QML consumers SHALL resolve them through the existing `TranslationManager.translate(key, fallback)` path, preserving today's translation keys and live language-switch behavior. The web editor SHALL use the English fallbacks, matching its current English-only presentation.

#### Scenario: Language switch still updates palette labels

- **WHEN** the app language changes while the layout editor is open
- **THEN** palette and chip labels re-resolve through their existing translation keys, exactly as before this change

#### Scenario: Unknown type degrades gracefully

- **WHEN** a saved layout contains a type absent from the catalog (e.g. from a newer app version)
- **THEN** consumers SHALL fall back to showing the raw type string, without errors

### Requirement: Recipes widget catalog entry
The widget catalog table SHALL gain a Recipes widget entry so the in-app palette, chip labels, library card, and web layout editor all derive it from the single table. The widget SHALL be registered in the standard places: the CMakeLists QML file list, the `LayoutItemDelegate` switch, `widgetCatalogTable()`, and `LayoutCenterZone` (the widget is allowed in center/idle zones like the Beans widget).

#### Scenario: Widget appears in the palette
- **WHEN** the user opens the layout editor
- **THEN** the Recipes widget is available in the palette and can be placed in bar and center zones

#### Scenario: Web editor parity
- **WHEN** the web layout editor loads the catalog
- **THEN** the Recipes widget appears with the same label and zone rules as in-app

