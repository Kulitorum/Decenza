# layout-brew-widgets Specification

## Purpose
Defines a set of home-screen layout widgets for brewing context — profile name, measured dose weight, measured milk weight, and a ratio quick-select pill that opens the ratio chooser and applies a chosen preset live to the stop-at-weight target — each placeable through the standard layout palette registration points.
## Requirements
### Requirement: Profile name widget

The layout palette SHALL provide a `profileName` widget that displays the current espresso profile name. It SHALL be registered in all four widget-registration locations and SHALL be placeable in any zone that accepts readout widgets.

#### Scenario: Shows the active profile

- **WHEN** an espresso profile is active
- **THEN** the widget SHALL display that profile's name

#### Scenario: No profile available

- **WHEN** no profile name is available
- **THEN** the widget SHALL display a placeholder ("—")

### Requirement: Measured dose widget

The layout palette SHALL provide a `doseWeight` widget that displays the measured dose weight (`Settings.dye.dyeBeanWeight`).

#### Scenario: Dose has been measured

- **WHEN** a dose weight greater than zero is recorded
- **THEN** the widget SHALL display the dose in grams (e.g. "18.0 g")

#### Scenario: No dose recorded

- **WHEN** no dose weight is recorded
- **THEN** the widget SHALL display a placeholder ("—")

### Requirement: Measured milk widget

The layout palette SHALL provide a `milkWeight` widget that displays the most recent measured milk weight.

#### Scenario: Milk has been measured

- **WHEN** a milk weight greater than zero is available
- **THEN** the widget SHALL display the milk weight in grams

#### Scenario: No milk measured or weight-timed steaming absent

- **WHEN** no milk weight is available (including when the weight-timed-steaming feature is not present)
- **THEN** the widget SHALL display a placeholder ("—") and SHALL NOT error

### Requirement: Ratio quick-select widget

The layout palette SHALL provide a `ratioQuickSelect` widget showing the current ratio as a `1:X.X` pill, which opens the ratio chooser (`RatioPresetDialog`) when tapped. Selecting a preset SHALL apply it live: record `Settings.brew.lastUsedRatio` and set the stop-at-weight target to `dose × ratio` using the measured dose, so the scale, Brew Settings and machine target reflect it at once. The pill SHALL follow the Brew pill transparency rule.

#### Scenario: Displays the current ratio

- **WHEN** the widget is rendered
- **THEN** it SHALL display `1:` followed by the current `lastUsedRatio` to one decimal place

#### Scenario: Tap opens the ratio chooser

- **WHEN** the user taps the widget
- **THEN** the ratio chooser SHALL open with the editable Ristretto / Normale / Lungo presets

#### Scenario: Selecting a preset applies the ratio live

- **WHEN** the user picks a ratio preset
- **THEN** `Settings.brew.lastUsedRatio` SHALL be updated
- **AND** the stop-at-weight target SHALL be recomputed as `dose × ratio` (using the measured dose, defaulting to 18 g when none is recorded)
- **AND** the new ratio SHALL be reflected in the scale widget, Brew Settings, and the machine target weight

#### Scenario: Transparent over a background image

- **GIVEN** a background image is configured
- **WHEN** the Ratio pill is rendered
- **THEN** the pill fill SHALL be transparent and its value text SHALL read against the background

#### Scenario: Accessible as a button

- **WHEN** a screen reader inspects the widget
- **THEN** it SHALL expose a button role, a name conveying the current ratio, and a "tap to change" hint

### Requirement: Grind quick-select widget

The layout palette SHALL provide a `grindQuickSelect` widget that displays the current grinder dial-in as a pill and, when tapped, opens a value picker (`GrindPickerDialog`). For a variable-RPM grinder the widget SHALL present both the burr grind setting and the motor RPM, not toggle between them. The pill SHALL show the grind setting alone for a non-RPM grinder or when no RPM is recorded, and `"<grind> · <rpm>"` otherwise.

#### Scenario: Step reflects the grinder's observed increments

- **GIVEN** the selected grinder's history contains numeric settings that step in 0.25 increments (e.g. 7.5, 8, 8.5, 8.75, 9)
- **WHEN** the widget builds its candidate rows for a current setting of `9`
- **THEN** the offered values SHALL step by `0.25` (…8.5, 8.75, 9, 9.25…), not by whole numbers

#### Scenario: No grinder selected falls back to full history

- **WHEN** no grinder is selected (no active grinder model)
- **THEN** the step SHALL be derived from the full observed shot history across grinders
- **AND** when the history yields fewer than two distinct numeric settings, the step SHALL default to `1.0`

#### Scenario: Noise-filtered step ignores a lone outlier

- **GIVEN** the grinder's history is 7.5, 8, 8.5, 8.75, 9 plus a single mistyped `8.1`
- **WHEN** the step is derived
- **THEN** the derived step SHALL remain `0.25` (the outlier's gaps SHALL NOT collapse the step to `0.1`)

#### Scenario: RPM step reflects the grinder's observed RPM history

- **GIVEN** a variable-RPM grinder whose logged RPMs step in ~50-RPM increments
- **WHEN** the widget builds its RPM candidate rows
- **THEN** the offered RPMs SHALL step by the derived increment
- **AND** when RPM history is too thin to derive, the step SHALL fall back to the fixed default (50)

#### Scenario: Combined pill shows both grind and RPM

- **GIVEN** a variable-RPM grinder with grind `8.75` and RPM `900` recorded
- **WHEN** the pill is rendered
- **THEN** it SHALL display both values (e.g. `8.75 · 900`)
- **AND** tapping it SHALL open a picker with a Grind section and an RPM section
- **AND** committing a Grind value SHALL change only the grind setting; committing an RPM value SHALL change only the RPM

#### Scenario: Non-RPM grinder shows grind only

- **GIVEN** a grinder that is not RPM-capable
- **WHEN** the pill is rendered and tapped
- **THEN** the pill SHALL show the grind setting alone and the picker SHALL contain only the Grind section

#### Scenario: Accessible as a button

- **WHEN** a screen reader inspects the widget
- **THEN** the pill SHALL expose a Button role, an accessible name including the current value, and a press action that opens the picker

#### Scenario: No grind set opens the picker ready to type

- **GIVEN** no grind value is set and the grinder has no observed history
- **WHEN** the pill is tapped
- **THEN** the picker SHALL open in text mode with the grind field focused
- **AND** it SHALL NOT show the previous "set a grind value in Brew Settings first" message

### Requirement: Grind picker writes one half per section

The picker SHALL contain a Grind section and, when the grinder is RPM-capable, an RPM section. Committing a value in a section, by picking a row or typing it (see `grind-value-entry`), SHALL write only that half, `Settings.dye.dyeGrinderSetting` or `Settings.dye.dyeGrinderRpm`, through the Brew Settings write-through path. RPM capability SHALL be determined by `Settings.dye.grinderRpmCapable(brand, model)`, matching the Brew dialog.

#### Scenario: Typed value writes only its half

- **GIVEN** the picker is in text mode for a variable-RPM grinder
- **WHEN** the user types an RPM value and commits it
- **THEN** `Settings.dye.dyeGrinderRpm` SHALL change
- **AND** `Settings.dye.dyeGrinderSetting` SHALL NOT change

### Requirement: Grind and RPM steps derive from shot history

Each section's step between candidate values SHALL be derived from the user's own shot history, not from a configured constant: the Grind step from the selected grinder's observed settings and the RPM step from its observed RPMs, using the same noise-filtered estimator. The widget SHALL NOT read `grindQuickSelectStep`. When history is too thin to derive a step, the Grind step SHALL default to `1.0` and the RPM step to `50`.

#### Scenario: Step is scoped to the selected grinder

- **GIVEN** two grinders whose histories step by different increments
- **WHEN** the widget derives the Grind step with the first grinder selected
- **THEN** the step SHALL come only from the first grinder's history

### Requirement: Grind widget reuses the shared grind-entry components

The widget SHALL obtain its candidate rows and stepping behaviour from the shared grind-entry components rather than owning that logic. When no rows can be generated it SHALL NOT display a message directing the user to set a grind value elsewhere; it SHALL open the picker in text mode instead (see `grind-value-entry`).

#### Scenario: Shared components supply the rows

- **WHEN** the widget builds its candidate rows
- **THEN** the rows SHALL come from the shared grind-entry component, not from logic inside the widget

### Requirement: Brew quick-select pills render transparently over a background image

The Grind and Ratio quick-select pills SHALL render with a transparent fill when a background image is set (`Settings.theme.backgroundImagePath` is non-empty), like the Beans and Milk widgets. With no background image they SHALL keep their solid capsule unchanged. When transparent, the value text SHALL use the zone text colour rather than the accent colour. Any override-highlight state on the value SHALL take precedence in both modes.

#### Scenario: Transparent over a background image

- **GIVEN** a background image is configured
- **WHEN** the Grind or Ratio pill is rendered
- **THEN** the pill fill SHALL be transparent
- **AND** the value text SHALL be legible against the background image (the zone text color, not the accent color that assumes a solid fill)

#### Scenario: Solid pill with no background image

- **GIVEN** no background image is configured
- **WHEN** the Grind or Ratio pill is rendered
- **THEN** the pill SHALL keep its existing solid capsule fill and accent value text, unchanged

