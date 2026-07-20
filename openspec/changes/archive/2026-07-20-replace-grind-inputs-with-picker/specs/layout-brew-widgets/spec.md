## MODIFIED Requirements

### Requirement: Grind quick-select widget

The layout palette SHALL provide a `grindQuickSelect` widget that displays the current grinder dial-in as a pill and, when tapped, opens a value picker (`GrindPickerDialog`). For a variable-RPM grinder the dial-in has two components — the burr grind setting AND the motor RPM — and the widget SHALL present BOTH rather than toggling between them. The pill SHALL display the grind setting alone for a non-RPM grinder (or when no RPM is recorded), and both values as `"<grind> · <rpm>"` when the grinder is RPM-capable and an RPM is set. The picker SHALL contain a Grind section and, when the grinder is RPM-capable, an RPM section; committing a value in a section — whether by picking a candidate row or by typing it in the picker's text mode (see `grind-value-entry`) — SHALL write only that half — `Settings.dye.dyeGrinderSetting` or `Settings.dye.dyeGrinderRpm` — using the same write-through path as the Brew Settings controls. RPM-capability SHALL be determined by the broad `Settings.dye.grinderRpmCapable(brand, model)` (matching the Brew dialog), not the narrower catalog-confirmed check.

Each section's step between candidate values SHALL be derived from the user's own shot history rather than a user-configured constant: the Grind step from the grinder's observed settings, the RPM step from the grinder's observed RPMs, via the same noise-filtered estimator. The widget SHALL request each step from the shot-history store scoped to the currently selected grinder, and SHALL NOT read any `grindQuickSelectStep` setting (that setting is removed by this change). When history is too thin to derive, the Grind step SHALL default to `1.0` and the RPM step to `50`.

The widget SHALL obtain its candidate rows and its stepping behaviour from the shared grind-entry components rather than owning that logic itself, and SHALL NOT display a message directing the user to set a grind value elsewhere when no rows can be generated — that state opens the picker in text mode instead (see `grind-value-entry`).

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
