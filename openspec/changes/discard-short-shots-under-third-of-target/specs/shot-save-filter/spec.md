## MODIFIED Requirements

### Requirement: The application SHALL classify and discard espresso shots that did not start

When an espresso extraction reaches the save path, the application SHALL
classify the shot as *aborted* if and only if `extractionDuration < 10.0 s` AND
`finalWeight` is below the yield threshold. The yield threshold SHALL be the
larger of `5.0 g` and one third of the weight the shot was set to stop at; a shot with
no weight target SHALL use `5.0 g`. A long, low-yield shot (e.g. 1 g over 60 s)
MUST NOT be classified as aborted. When the verdict is *aborted*, the
application SHALL skip saving the shot to `ShotHistoryStorage` and SHALL skip
any visualizer auto-upload for it.

#### Scenario: Canonical preinfusion abort is discarded

- **GIVEN** an espresso shot ends with `extractionDuration = 2.3 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *aborted*
- **AND** `ShotHistoryStorage::saveShot()` SHALL NOT be called for this shot
- **AND** any visualizer auto-upload SHALL NOT run for this shot
- **AND** the application SHALL emit a UI signal that a discard occurred

#### Scenario: A rinse that wets the scale is discarded

- **GIVEN** an espresso cycle ends with `extractionDuration = 1.5 s` and `finalWeight = 6.4 g` against a 32 g stop target
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *aborted* (6.4 g is under a third of 32 g)

#### Scenario: No weight target falls back to the fixed threshold

- **GIVEN** an espresso shot with no weight target ends with `extractionDuration = 1.5 s` and `finalWeight = 6.4 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (yield ≥ 5 g)

#### Scenario: Long, low-yield choke is preserved

- **GIVEN** an espresso shot ends with `extractionDuration = 59.6 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (duration ≥ 10 s)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Short shot with real yield is preserved

- **GIVEN** an espresso shot ends with `extractionDuration = 7.3 s` and `finalWeight = 37.4 g` (turbo-style)
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (yield at or above a third of the target)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Boundary — exactly at threshold is preserved

- **GIVEN** an espresso shot ends with `extractionDuration = 10.0 s` and `finalWeight = 4.9 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* because the duration clause uses strict `<` (not `<=`)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Non-espresso paths are not classified

- **GIVEN** a steam, hot water, or flush operation completes
- **WHEN** the operation's end-of-cycle handler runs
- **THEN** the aborted-shot classifier SHALL NOT be invoked
- **AND** the operation's existing save behavior (or absence of save) SHALL be unchanged

#### Scenario: Every classifier evaluation is logged

- **WHEN** an espresso shot ends and the classifier runs
- **THEN** the application SHALL log a single line via the async logger containing: `extractionDuration` (seconds, 3 decimal places), `finalWeight` (grams, 1 decimal place), the verdict (`aborted` or `kept`), and the action (`discarded` or `saved`)

### Requirement: The aborted-shot thresholds are fixed and the classifier is unconditional

The thresholds (`10.0 s`, `5.0 g`, one third of the stop target) SHALL be hard-coded
constants in the C++ source and SHALL NOT be user-tunable settings. The
classifier SHALL run unconditionally, with no user-facing opt-out.

#### Scenario: No setting disables or retunes the classifier

- **WHEN** the user reviews the application settings
- **THEN** no setting exposes the thresholds or turns the classifier off
