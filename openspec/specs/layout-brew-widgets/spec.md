# layout-brew-widgets Specification

## Purpose
TBD - created by archiving change composable-brew-bar. Update Purpose after archive.
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

The layout palette SHALL provide a `ratioQuickSelect` widget that displays the current coffee-to-water ratio as a `1:X.X` pill and, when tapped, opens the ratio chooser (`RatioPresetDialog`). Selecting a preset SHALL set only `Settings.brew.lastUsedRatio` and SHALL NOT modify the persistent profile target or `brewYieldOverride`.

#### Scenario: Displays the current ratio

- **WHEN** the widget is rendered
- **THEN** it SHALL display `1:` followed by the current `lastUsedRatio` to one decimal place

#### Scenario: Tap opens the ratio chooser

- **WHEN** the user taps the widget
- **THEN** the ratio chooser SHALL open with the editable Ristretto / Normale / Lungo presets

#### Scenario: Selecting a preset updates only the live ratio

- **WHEN** the user picks a ratio preset
- **THEN** `Settings.brew.lastUsedRatio` SHALL be updated
- **AND** the persistent profile target and `brewYieldOverride` SHALL be unchanged

#### Scenario: Accessible as a button

- **WHEN** a screen reader inspects the widget
- **THEN** it SHALL expose a button role, a name conveying the current ratio, and a "tap to change" hint

