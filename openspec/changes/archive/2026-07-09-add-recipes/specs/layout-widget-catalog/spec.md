# layout-widget-catalog (delta)

## ADDED Requirements

### Requirement: Recipes widget catalog entry
The widget catalog table SHALL gain a Recipes widget entry so the in-app palette, chip labels, library card, and web layout editor all derive it from the single table. The widget SHALL be registered in the standard places: the CMakeLists QML file list, the `LayoutItemDelegate` switch, `widgetCatalogTable()`, and `LayoutCenterZone` (the widget is allowed in center/idle zones like the Beans widget).

#### Scenario: Widget appears in the palette
- **WHEN** the user opens the layout editor
- **THEN** the Recipes widget is available in the palette and can be placed in bar and center zones

#### Scenario: Web editor parity
- **WHEN** the web layout editor loads the catalog
- **THEN** the Recipes widget appears with the same label and zone rules as in-app
