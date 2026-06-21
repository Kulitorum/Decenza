## Why

The zone layout editor (both the in-app QML editor and the ShotServer web editor) relies on interactions users have to discover by accident. Reordering widgets means selecting a widget and tapping small left/right arrows — slow and unlike the drag-and-drop people expect. Worse, the only way to configure a widget is a long-press (in-app) or a click that silently opens an editor (web), with **no visible indication of which widgets even have options**, so users guess. These rough edges make a powerful feature feel hidden and fiddly.

## What Changes

- **Drag-and-drop reordering** replaces the select-then-arrow interaction for moving widgets within a zone, in **both** the in-app and web editors. The `◀`/`▶` arrow buttons are removed.
- **A persistent "has options" affordance** is shown on every configurable widget chip in both editors (a small settings/gear indicator), so users can tell at a glance which widgets are configurable without guessing.
- **An explicit, visible way to open a widget's options** (tapping the indicator / a selected chip's options button) is added alongside the existing long-press, so discovery no longer depends on knowing the hidden gesture. Long-press is retained as a shortcut.
- **A single source of truth** for "does this widget type have options" is introduced so the indicator, the open-options affordance, and the long-press gate stay in sync (today the list of configurable types is duplicated across at least three places).
- **Editor parity:** the web and in-app editors gain the same affordances (drag-to-reorder, options indicator) so the two experiences match.
- **Reuse existing, better-fitting QML components** instead of the editor's current raw `Text`+`MouseArea` chips: adopt the project's proven drag-reorder mechanics (the `FavoritesListView` pattern), `StyledIconButton`/`AccessibleTapHandler`, and existing icon assets (`settings.svg`, `cross.svg`, `list.svg`). This also fixes pre-existing accessibility violations in the file.

Additional usability fixes (both editors where applicable):

- **Accurate guidance:** update the editor's help/instruction text — today it says "Tap a widget to select it for moving or reordering. Long-press Custom items to edit," which is wrong after this change (no arrows; many widget types are configurable, not just Custom).
- **Confirm destructive actions:** "Reset to Default" currently wipes the entire layout on a single tap with no confirmation (while the far-less-destructive library delete already confirms). Add a confirmation, and confirm removal of a *configured* widget so a setup isn't lost by accident.
- **Better widget picker:** the add-widget list is a flat ~33-item list grouped only by text color. Give it labeled category headings, sort widgets predictably within each category, and add a type-to-filter field.
- **Accessible, consistent controls:** replace the remaining raw `Rectangle`+`MouseArea` controls and Unicode-glyph icons in the touched editor files (zone `▲`/`▼`/`−` move-and-scale, the add `+`, zone-options, the Library `▣` grid toggle) with `AccessibleButton`/`StyledIconButton` + SVG icons.
- **One options editor at a time:** opening a widget's options closes any other open options editor, avoiding stacked/ambiguous popups.

## Capabilities

### New Capabilities
- `layout-editor-reordering`: Drag-and-drop reordering of widgets within a zone in both the in-app and web layout editors, persisting through the existing reorder mechanism.
- `layout-editor-usability`: Editor-wide usability requirements — accurate guidance text, confirmation for destructive actions, a discoverable/sorted/filterable widget picker, accessible and consistent controls, and a single active options editor.

### Modified Capabilities
- `layout-widget-instance-config`: Add a discoverability requirement — configurable widget instances SHALL show a visible "has options" indicator and offer an explicit visible affordance to open their options (in addition to the existing long-press), with a single source of truth for which widget types are configurable.

## Impact

- **QML (in-app editor):**
  - `qml/pages/settings/LayoutEditorZone.qml` — remove arrow buttons; rebuild chip interactions on the `FavoritesListView` drag pattern (`DelegateModel` + `Drag`/`DropArea` live-swap + `Flow.move` transition) kept on the wrapping `Flow`; replace raw `Text`+`MouseArea` controls with `StyledIconButton`/`AccessibleTapHandler`; add the `settings.svg` options indicator/affordance.
  - `qml/pages/settings/SettingsLayoutTab.qml` — reorder signal wiring; options-open routing (already centralizes `openCustomEditor`).
  - Reuses existing components (`FavoritesListView` pattern, `StyledIconButton`, `AccessibleTapHandler`, `AccessibleButton`) and existing icons (`settings.svg`, `cross.svg`, `list.svg`) — no new QML files or icon assets required.
  - A small shared helper (C++ method on `SettingsNetwork`) exposing "type has options".
- **Web editor:**
  - `src/network/shotserver_layout.cpp` — replace arrow generation with drag-and-drop (HTML5 DnD or pointer-based), add the options indicator, reuse the existing `/api/layout/reorder` endpoint; update guidance text, add reset confirmation, and group/sort/filter the widget picker for parity.
- **Page-level (in-app):**
  - `qml/pages/settings/SettingsLayoutTab.qml` — instruction text, reset-to-default confirmation dialog, single-active-options-editor coordination.
  - `qml/components/library/LibraryPanel.qml` — replace the `▣` Unicode grid-toggle glyph and any raw controls touched with SVG icons / shared accessible components.
  - The add-widget picker (in `LayoutEditorZone.qml`) — category headings, within-category sorting, and a filter field.
- **Model / backend:** No data-model change required — `reorderItem()` and the item-property/`/api/layout/item` mechanisms already exist; reordering and option persistence are unchanged. The single-source-of-truth for "has options" may live as a C++ helper on `SettingsNetwork` (or a QML singleton) consumed by both editors.
- **Accessibility:** drag interactions need accessible fallbacks/labels; the new options indicator needs `Accessible.role`/`name`. Per project rules, the indicator must be an SVG `Image` from `qrc:/icons/`, not a Unicode glyph.
- **No new user-facing settings** are introduced.
