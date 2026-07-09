# layout-lower-mid-bar-zone Specification

## Purpose
Defines the optional `lowerMidBar` layout zone, a general-purpose full-width band rendered above the bottom action bar on the idle/home screen: it accepts any palette widget, collapses to zero height when empty so existing layouts are visually unaffected, and is shown or hidden at runtime based on available viewport height rather than a hardcoded device list.

## Requirements
### Requirement: Optional lower-mid-bar layout zone

The layout system SHALL provide a general-purpose, full-width layout zone named `lowerMidBar` that renders directly above the existing bottom action bar on the idle/home screen. The zone SHALL accept any widget the layout system supports (it is location-named, not content-specific). It SHALL be registered everywhere existing zones are registered: the default layout JSON, the layout-migration path, the in-app layout editor, and the web layout editor.

#### Scenario: Zone is empty by default

- **WHEN** a user has no saved layout, or an existing layout that predates this change
- **THEN** the `lowerMidBar` zone SHALL exist and contain zero widgets
- **AND** the rendered home screen SHALL be visually identical to before this change (no reserved space, no visible bar)

#### Scenario: Zone accepts any widget type

- **WHEN** a user adds widgets to `lowerMidBar` in either editor
- **THEN** the zone SHALL accept any palette widget, the same as other bar zones

#### Scenario: Zone is configurable in both editors

- **WHEN** a user opens the in-app layout editor or the web layout editor
- **THEN** `lowerMidBar` SHALL appear as a selectable zone
- **AND** the user SHALL be able to add, remove, and reorder widgets in it using the same controls as every other zone

### Requirement: Lower-mid-bar zone collapses when empty

The `lowerMidBar` zone SHALL reserve no vertical space when it contains no widgets, so the center content and bottom action bar keep their current positions.

#### Scenario: Empty zone reserves no space

- **WHEN** the `lowerMidBar` zone contains zero widgets
- **THEN** its rendered height SHALL be zero
- **AND** the bottom action bar SHALL sit flush at the bottom as it does today

#### Scenario: Populated zone reserves height

- **WHEN** the `lowerMidBar` zone contains one or more widgets
- **THEN** the bar SHALL render as a full-width band above the bottom action bar sized to its content

### Requirement: Lower-mid-bar zone is height-gated at runtime

The `lowerMidBar` zone SHALL render only when the viewport has enough vertical room after accounting for the fixed bars (top status bar, center content minimum, bottom action bar). The decision SHALL be based on available height at runtime, not on a hardcoded device list.

#### Scenario: Hidden on a short viewport

- **WHEN** the available center height is less than the bar height plus the minimum center content height
- **THEN** the `lowerMidBar` zone SHALL be hidden and reserve no space
- **AND** the rest of the home screen SHALL lay out as if the zone were empty

#### Scenario: Shown on a tall-enough viewport

- **WHEN** the available center height can fit the bar plus the minimum center content
- **AND** the `lowerMidBar` zone contains one or more widgets
- **THEN** the bar SHALL render above the bottom action bar

#### Scenario: Configuration is preserved across viewports

- **WHEN** a user has configured widgets in `lowerMidBar` and the zone is hidden because the viewport is too short
- **THEN** the configured widgets SHALL remain saved
- **AND** the zone SHALL reappear with the same widgets when the viewport is tall enough again
- **AND** the layout editor SHALL still expose the zone for configuration regardless of runtime fit

