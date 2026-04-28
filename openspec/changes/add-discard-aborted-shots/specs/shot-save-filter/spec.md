# shot-save-filter

## ADDED Requirements

### Requirement: The application SHALL classify and discard espresso shots that did not start

When an espresso extraction ends and reaches the save path, the application SHALL classify the shot as *aborted* iff BOTH of the following hold: `extractionDuration < 10.0 s` AND `finalWeight < 5.0 g`. The two clauses form a conjunction; either alone is insufficient to classify the shot as aborted. Long-running low-yield shots (e.g. a choked puck producing 1 g over 60 s) MUST NOT classify as aborted, because their graphs are diagnostically valuable.

When the `Settings.brew.discardAbortedShots` toggle is `true` (default) AND the classifier returns *aborted*, the application SHALL skip persisting the shot to `ShotHistoryStorage` AND SHALL skip any visualizer auto-upload for that shot. When the toggle is `false`, the classifier SHALL NOT be consulted and all shots SHALL save as they do today.

The classification SHALL apply only to shots that flow through the espresso save path (`MainController::endShot()` with `m_extractionStarted == true`). Steam, hot water, flush, and cleaning operations are out of scope and SHALL not be evaluated against this classifier.

The two threshold values (`10.0 s`, `5.0 g`) SHALL be hard-coded constants in the C++ source. They SHALL NOT be exposed as user-tunable settings; the only user-facing control is the boolean discard toggle.

#### Scenario: Canonical preinfusion abort is discarded

- **GIVEN** the discard toggle is enabled
- **AND** an espresso shot ends with `extractionDuration = 2.3 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *aborted*
- **AND** `ShotHistoryStorage::saveShot()` SHALL NOT be called for this shot
- **AND** any visualizer auto-upload SHALL NOT run for this shot
- **AND** the application SHALL emit a UI signal that a discard occurred

#### Scenario: Long, low-yield choke is preserved

- **GIVEN** the discard toggle is enabled
- **AND** an espresso shot ends with `extractionDuration = 59.6 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (duration ≥ 10 s)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Short shot with real yield is preserved

- **GIVEN** the discard toggle is enabled
- **AND** an espresso shot ends with `extractionDuration = 7.3 s` and `finalWeight = 37.4 g` (turbo-style)
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (yield ≥ 5 g)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Toggle off bypasses the classifier entirely

- **GIVEN** the discard toggle is disabled (`Settings.brew.discardAbortedShots == false`)
- **AND** an espresso shot ends with `extractionDuration = 2.3 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL NOT be evaluated
- **AND** the shot SHALL be saved normally to history

#### Scenario: Boundary — exactly at threshold is preserved

- **GIVEN** the discard toggle is enabled
- **AND** an espresso shot ends with `extractionDuration = 10.0 s` and `finalWeight = 4.9 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* because the duration clause uses strict `<` (not `<=`)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Non-espresso paths are not classified

- **GIVEN** a steam, hot water, or flush operation completes
- **WHEN** the operation's end-of-cycle handler runs
- **THEN** the aborted-shot classifier SHALL NOT be invoked
- **AND** the operation's existing save behavior (or absence of save) SHALL be unchanged

#### Scenario: Every classifier evaluation is logged

- **GIVEN** the discard toggle is enabled
- **WHEN** an espresso shot ends and the classifier runs
- **THEN** the application SHALL log a single line via the async logger containing: `extractionDuration` (seconds, 3 decimal places), `finalWeight` (grams, 1 decimal place), the verdict (`aborted` or `kept`), and the action (`discarded`, `saved`, or `saved-anyway`)

---

### Requirement: The application SHALL surface a toast with a "Save anyway" action whenever a shot is discarded

When the classifier discards a shot, the application SHALL display a toast on the currently visible page (Espresso, Idle, or whichever page the user lands on at shot-end). The toast SHALL show the message `"Shot did not start — not recorded"` (translated via `TranslationManager`) and SHALL include a single action button labelled `"Save anyway"`. The toast SHALL auto-dismiss after a fixed timeout (timer is permitted here per the project's UI auto-dismiss carve-out).

When the user taps `Save anyway`, the application SHALL persist the discarded shot to `ShotHistoryStorage` using the same metadata that would have been saved on the original code path, AND SHALL run the visualizer auto-upload if the user has it enabled. The save SHALL be a one-shot operation per discarded shot — once saved-anyway, the toast SHALL dismiss and the action SHALL not be re-triggerable for the same shot.

If the user does not tap the action before the toast dismisses, the shot SHALL remain unsaved with no further opportunity to recover it (the in-memory pending shot data is discarded).

#### Scenario: Toast appears on shot-end page

- **GIVEN** an aborted shot is discarded by the classifier
- **WHEN** the discard signal fires
- **THEN** a toast SHALL appear on the current top page of the QML stack
- **AND** the toast text SHALL be the translated equivalent of `"Shot did not start — not recorded"`
- **AND** the toast SHALL include a `"Save anyway"` action button

#### Scenario: Save anyway persists the shot with original metadata

- **GIVEN** the toast is visible and not yet dismissed
- **WHEN** the user taps `Save anyway`
- **THEN** the application SHALL persist the shot to `ShotHistoryStorage` with the same `ShotMetadata`, `doseWeight`, `finalWeight`, `duration`, and `debugLog` that the original save would have used
- **AND** the application SHALL run visualizer auto-upload if `Settings.visualizer.visualizerAutoUpload == true`
- **AND** the toast SHALL dismiss
- **AND** subsequent presses of any UI element SHALL not re-trigger the save (the action is one-shot)

#### Scenario: Toast auto-dismisses without action

- **GIVEN** the toast is visible
- **WHEN** the auto-dismiss timeout elapses with no user action
- **THEN** the toast SHALL hide
- **AND** the discarded shot's in-memory data SHALL be released
- **AND** there SHALL be no further opportunity to recover the shot from this session
