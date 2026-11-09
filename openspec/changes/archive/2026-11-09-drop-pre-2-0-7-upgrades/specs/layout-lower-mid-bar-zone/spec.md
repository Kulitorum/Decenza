# Spec Delta

## MODIFIED Requirements

### Requirement: Optional lower-mid-bar layout zone

The layout system SHALL provide a general-purpose, full-width layout zone named `lowerMidBar` that renders directly above the existing bottom action bar on the idle/home screen. The zone SHALL accept any widget the layout system supports (it is location-named, not content-specific). It SHALL be registered everywhere existing zones are registered: the default layout JSON, the in-app layout editor, and the web layout editor. A saved layout without a `lowerMidBar` entry SHALL behave as one whose zone is empty.

#### Scenario: Zone is empty by default

- **WHEN** a user has no saved layout, or an existing layout that predates this change
- **THEN** the `lowerMidBar` zone SHALL behave as present and containing zero widgets
- **AND** the rendered home screen SHALL be visually identical to before this change (no reserved space, no visible bar)

#### Scenario: Zone accepts any widget type

- **WHEN** a user adds widgets to `lowerMidBar` in either editor
- **THEN** the zone SHALL accept any palette widget, the same as other bar zones

#### Scenario: Zone is configurable in both editors

- **WHEN** a user opens the in-app layout editor or the web layout editor
- **THEN** `lowerMidBar` SHALL appear as a selectable zone
- **AND** the user SHALL be able to add, remove, and reorder widgets in it using the same controls as every other zone
