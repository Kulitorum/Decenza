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

### Requirement: Per-instance color for the Clock widget

The Clock widget SHALL gain a per-instance `color` property selectable from a fixed set: `white` (default), `green`, `red`, `blue`, and `orange`. Each non-default value SHALL map to an existing semantic theme color used elsewhere on the page (so the clock matches the surrounding readouts and honours custom themes): `white` → theme text color, `green` → pressure color, `red` → temperature color, `blue` → flow color, `orange` → warning color. The chosen color SHALL tint both the time text and the optional clock icon. The value SHALL be read from the item's stored properties (`modelData`), persist per instance, and apply in any zone the widget is placed in.

#### Scenario: Default color when unset

- **WHEN** a Clock widget has no `color` set (existing layouts, or a freshly added widget)
- **THEN** it SHALL render in the theme text color exactly as it does today

#### Scenario: Applying a chosen color

- **WHEN** a Clock widget's `color` is set to `green`, `red`, `blue`, or `orange`
- **THEN** both its time text and (in icon mode) its clock icon SHALL be tinted with the mapped theme color

#### Scenario: Per-instance independence

- **WHEN** two Clock widgets are placed with different `color` values
- **THEN** each SHALL render in its own color independently

### Requirement: Clock color is editable in both editors

The per-instance options editor for the Clock widget SHALL expose the `color` choice alongside the existing `displayMode` (text/icon) choice, in both the native QML editor and the web layout editor. Selecting a color SHALL persist it to the item's properties immediately and update the rendered widget.

#### Scenario: Editing color in the native editor

- **WHEN** the user opens the options editor for a Clock widget and selects a color
- **THEN** the color SHALL be saved to that instance and the widget SHALL update to the chosen color

#### Scenario: Editing color in the web editor

- **WHEN** the user selects a color for a Clock chip in the web layout editor
- **THEN** the color SHALL be persisted to that instance and reflected in the layout
