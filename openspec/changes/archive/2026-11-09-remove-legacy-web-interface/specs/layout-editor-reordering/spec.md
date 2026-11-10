# Spec Delta

## MODIFIED Requirements

### Requirement: Drag-and-drop reordering of widgets within a zone

Both the in-app layout editor and the web layout editor SHALL let a user reorder the widgets within a zone by dragging a widget to a new position in that zone. Dragging SHALL replace the prior select-then-arrow interaction as the primary reordering method. Reordering SHALL persist through the existing reorder mechanism (through the shared layout service) so existing layouts and storage are unchanged.

#### Scenario: Drag a widget to a new position in-app

- **WHEN** a user presses a widget chip in the in-app editor and drags it onto another position within the same zone
- **THEN** the widget SHALL move to the drop position and the other widgets SHALL shift to accommodate it
- **AND** the new order SHALL be persisted and SHALL survive an app restart

#### Scenario: Drag a widget to a new position in the web editor

- **WHEN** a user drags a widget chip to a new position within the same zone in the web editor
- **THEN** the shared editor SHALL reorder the widget and re-render the zone in the new order
- **AND** the new order SHALL be persisted

#### Scenario: Reorder produces the same result as before

- **WHEN** a user drags a widget from index `i` to index `j` within a zone
- **THEN** the resulting widget order SHALL be identical to what the previous left/right-arrow steps would have produced for the same start and end positions

#### Scenario: Drag within a zone only

- **WHEN** a user drags a widget
- **THEN** reordering SHALL be constrained to the widget's own zone (this change does not introduce cross-zone dragging)
- **AND** a drag released outside any valid position SHALL leave the order unchanged

