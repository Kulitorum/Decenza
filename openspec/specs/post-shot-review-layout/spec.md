# post-shot-review-layout Specification

## Purpose
TBD - created by archiving change improve-grind-visibility-shot-review. Update Purpose after archive.
## Requirements
### Requirement: Equipment card appears last in the review field grid
The post-shot review page's field grid SHALL order its children as: editable per-shot fields (Grind setting, RPM, Barista), then read-only shot metadata (Preset, Shot date), then the equipment identity card (grinder, basket, puck prep, with its Change Equipment button) as the final item.

#### Scenario: Reviewing a shot after brewing
- **WHEN** the user scrolls to the field grid on the post-shot review page
- **THEN** the Grind setting, RPM, and Barista fields appear first, followed by the read-only Preset and Shot date displays, with the equipment identity card at the very bottom of the grid

#### Scenario: Changing equipment from the review page
- **WHEN** the user taps Change Equipment on the equipment card in its new bottom position
- **THEN** the equipment picker opens and re-points the shot's equipment package exactly as before the reorder

### Requirement: Review field grid reorder preserves behavior
Moving the equipment card SHALL NOT change any editing, autosave, undo, or accessibility behavior of the grid's fields; assistive-technology reading order SHALL follow the new visual order.

#### Scenario: Editing dial-in fields after the reorder
- **WHEN** the user edits the Grind setting or RPM field and moves focus away
- **THEN** the edit autosaves and participates in undo exactly as before the reorder

#### Scenario: Screen reader traverses the grid
- **WHEN** a screen reader traverses the post-shot review field grid
- **THEN** it reads the editable fields first, then Preset and Shot date, then the equipment card group last

