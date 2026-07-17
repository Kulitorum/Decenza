## ADDED Requirements

### Requirement: Grind quick-select widget

The layout palette SHALL provide a `grindQuickSelect` widget that displays the current grinder dial-in as a pill and, when tapped, opens a value picker (`GrindPickerDialog`). For a variable-RPM grinder the dial-in has two components — the burr grind setting AND the motor RPM — and the widget SHALL present BOTH rather than toggling between them. The pill SHALL display the grind setting alone for a non-RPM grinder (or when no RPM is recorded), and both values as `"<grind> · <rpm>"` when the grinder is RPM-capable and an RPM is set. The picker SHALL contain a Grind section and, when the grinder is RPM-capable, an RPM section; picking a row in a section SHALL write only that half — `Settings.dye.dyeGrinderSetting` or `Settings.dye.dyeGrinderRpm` — using the same write-through path as the Brew Settings controls. RPM-capability SHALL be determined by the broad `Settings.dye.grinderRpmCapable(brand, model)` (matching the Brew dialog), not the narrower catalog-confirmed check.

Each section's step between candidate values SHALL be derived from the user's own shot history rather than a user-configured constant: the Grind step from the grinder's observed settings, the RPM step from the grinder's observed RPMs, via the same noise-filtered estimator. The widget SHALL request each step from the shot-history store scoped to the currently selected grinder, and SHALL NOT read any `grindQuickSelectStep` setting (that setting is removed by this change). When history is too thin to derive, the Grind step SHALL default to `1.0` and the RPM step to `50`.

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
- **AND** picking a Grind row SHALL change only the grind setting; picking an RPM row SHALL change only the RPM

#### Scenario: Non-RPM grinder shows grind only

- **GIVEN** a grinder that is not RPM-capable
- **WHEN** the pill is rendered and tapped
- **THEN** the pill SHALL show the grind setting alone and the picker SHALL contain only the Grind section

#### Scenario: Accessible as a button

- **WHEN** a screen reader inspects the widget
- **THEN** the pill SHALL expose a Button role, an accessible name including the current value, and a press action that opens the picker

### Requirement: Brew quick-select pills render transparently over a background image

The Grind and Ratio quick-select pills SHALL render with a transparent fill when a background image is set (`Settings.theme.backgroundImagePath` is non-empty), so the value reads directly on the background like the Beans and Milk widgets. When no background image is set, the pills SHALL keep their existing solid capsule (zone-color fill, accent-colored value text) unchanged. When rendered transparently, the pill's value text SHALL use a color that reads against the background (the zone text color) rather than the accent color chosen for contrast against the solid fill; any override-highlight state on the value SHALL continue to take precedence in both modes.

#### Scenario: Transparent over a background image

- **GIVEN** a background image is configured
- **WHEN** the Grind or Ratio pill is rendered
- **THEN** the pill fill SHALL be transparent
- **AND** the value text SHALL be legible against the background image (the zone text color, not the accent color that assumes a solid fill)

#### Scenario: Solid pill with no background image

- **GIVEN** no background image is configured
- **WHEN** the Grind or Ratio pill is rendered
- **THEN** the pill SHALL keep its existing solid capsule fill and accent value text, unchanged

## MODIFIED Requirements

### Requirement: Ratio quick-select widget

The layout palette SHALL provide a `ratioQuickSelect` widget that displays the current coffee-to-water ratio as a `1:X.X` pill and, when tapped, opens the ratio chooser (`RatioPresetDialog`). Selecting a preset SHALL apply the ratio live: record `Settings.brew.lastUsedRatio` and recompute the stop-at-weight target (`brewYieldOverride = dose × ratio`, using the measured dose) so the new ratio is reflected immediately in the scale widget, Brew Settings, and the machine target. The pill SHALL follow the transparent-over-background-image rendering convention defined by the "Brew quick-select pills render transparently over a background image" requirement.

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
