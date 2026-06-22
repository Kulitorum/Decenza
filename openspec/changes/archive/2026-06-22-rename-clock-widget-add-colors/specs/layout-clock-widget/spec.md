## ADDED Requirements

### Requirement: Clock widget editor label reads "Time"

The Clock widget SHALL be presented to the user as **"Time"** in every editor surface, including the widget picker/catalog in both the native QML layout editor and the web (ShotServer) layout editor. The internal widget `type` identifier SHALL remain `clock`; only the user-visible label changes. The label SHALL match the placed-chip label and the center-zone caption, which already read "Time".

#### Scenario: Picker label in the native editor

- **WHEN** the user opens the add-widget picker in the native layout editor
- **THEN** the Clock widget entry SHALL be labelled "Time" (not "Clock")

#### Scenario: Picker label in the web editor

- **WHEN** the user opens the widget picker in the web layout editor
- **THEN** the Clock widget entry SHALL be labelled "Time" (not "Clock")

#### Scenario: Identifier unchanged

- **WHEN** a Time widget is added or an existing layout containing a `clock` item is loaded
- **THEN** the persisted widget `type` SHALL remain `clock` and existing layouts SHALL continue to work without migration
