# layout-editor-reordering Specification

## Purpose
Defines how widgets are reordered within a zone in the in-app and web layout editors: drag-and-drop as the primary interaction (persisted through the existing reorder mechanism), removal of the prior arrow-button controls, and an accessible move-toward-start/move-toward-end fallback for screen-reader and keyboard users who cannot perform drag gestures.

## Requirements
### Requirement: Drag-and-drop reordering of widgets within a zone

Both the in-app layout editor and the web layout editor SHALL let a user reorder the widgets within a zone by dragging a widget to a new position in that zone. Dragging SHALL replace the prior select-then-arrow interaction as the primary reordering method. Reordering SHALL persist through the existing reorder mechanism (`SettingsNetwork::reorderItem()` in-app and the `/api/layout/reorder` endpoint on the web) so existing layouts and storage are unchanged.

#### Scenario: Drag a widget to a new position in-app

- **WHEN** a user presses a widget chip in the in-app editor and drags it onto another position within the same zone
- **THEN** the widget SHALL move to the drop position and the other widgets SHALL shift to accommodate it
- **AND** the new order SHALL be persisted and SHALL survive an app restart

#### Scenario: Drag a widget to a new position in the web editor

- **WHEN** a user drags a widget chip to a new position within the same zone in the web editor
- **THEN** the editor SHALL reorder the widget via `/api/layout/reorder` and re-render the zone in the new order
- **AND** the new order SHALL be persisted

#### Scenario: Reorder produces the same result as before

- **WHEN** a user drags a widget from index `i` to index `j` within a zone
- **THEN** the resulting widget order SHALL be identical to what the previous left/right-arrow steps would have produced for the same start and end positions

#### Scenario: Drag within a zone only

- **WHEN** a user drags a widget
- **THEN** reordering SHALL be constrained to the widget's own zone (this change does not introduce cross-zone dragging)
- **AND** a drag released outside any valid position SHALL leave the order unchanged

### Requirement: Removal of the arrow-based reordering controls

The in-app and web editors SHALL no longer present the `◀`/`▶` move-left/move-right arrow buttons on a selected widget, since drag-and-drop replaces them. Removing a widget SHALL still be available through its existing remove control.

#### Scenario: No move arrows shown on selection in-app

- **WHEN** a user selects a widget chip in the in-app editor
- **THEN** no move-left or move-right arrow buttons SHALL be shown
- **AND** the remove control SHALL still be available

#### Scenario: No move arrows shown in the web editor

- **WHEN** a user selects a widget chip in the web editor
- **THEN** no move-left or move-right arrow controls SHALL be rendered
- **AND** the remove control SHALL still be available

### Requirement: Accessible reordering fallback

Because drag gestures are not operable with assistive technology, each widget chip SHALL expose accessible move actions (move toward the start / move toward the end of the zone) so a screen-reader or keyboard user can still reorder widgets. These actions SHALL call the same reorder mechanism as dragging.

#### Scenario: Screen-reader user reorders a widget

- **WHEN** a screen-reader user activates a widget chip's "move toward start" or "move toward end" accessible action
- **THEN** the widget SHALL move one position in that direction within its zone
- **AND** the action SHALL be unavailable (or a no-op) when the widget is already at that end of the zone

