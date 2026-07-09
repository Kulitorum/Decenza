# shot-save-filter Specification

## Purpose
Defines the classifier that discards espresso shots which never really started (extraction under 10 s AND final weight under 5 g) before they reach `ShotHistoryStorage` or visualizer auto-upload, with a toast notifying the user of the discard. Also covers the equipment and active-bag fields (`equipment_id`, rpm, `bagId`/`frozenDate`/`defrostDate`, dose/yield stamping) snapshotted onto shots that are kept.

## Requirements
### Requirement: The application SHALL classify and discard espresso shots that did not start

When an espresso extraction ends and reaches the save path, the application SHALL classify the shot as *aborted* iff BOTH of the following hold: `extractionDuration < 10.0 s` AND `finalWeight < 5.0 g`. The two clauses form a conjunction; either alone is insufficient to classify the shot as aborted. Long-running low-yield shots (e.g. a choked puck producing 1 g over 60 s) MUST NOT classify as aborted, because their graphs are diagnostically valuable.

When the classifier returns *aborted*, the application SHALL skip persisting the shot to `ShotHistoryStorage` AND SHALL skip any visualizer auto-upload for that shot. The classifier runs unconditionally — there is no user-facing opt-out.

The classification SHALL apply only to shots that flow through the espresso save path (`MainController::endShot()` with `m_extractionStarted == true`). Steam, hot water, flush, and cleaning operations are out of scope and SHALL not be evaluated against this classifier.

The two threshold values (`10.0 s`, `5.0 g`) SHALL be hard-coded constants in the C++ source. They SHALL NOT be exposed as user-tunable settings.

#### Scenario: Canonical preinfusion abort is discarded

- **GIVEN** an espresso shot ends with `extractionDuration = 2.3 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *aborted*
- **AND** `ShotHistoryStorage::saveShot()` SHALL NOT be called for this shot
- **AND** any visualizer auto-upload SHALL NOT run for this shot
- **AND** the application SHALL emit a UI signal that a discard occurred

#### Scenario: Long, low-yield choke is preserved

- **GIVEN** an espresso shot ends with `extractionDuration = 59.6 s` and `finalWeight = 1.1 g`
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (duration ≥ 10 s)
- **AND** the shot SHALL be saved normally to history

#### Scenario: Short shot with real yield is preserved

- **GIVEN** an espresso shot ends with `extractionDuration = 7.3 s` and `finalWeight = 37.4 g` (turbo-style)
- **WHEN** `endShot()` reaches the save path
- **THEN** the classifier SHALL return *kept* (yield ≥ 5 g)
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

---

### Requirement: The application SHALL surface a notification toast whenever a shot is discarded

When the classifier discards a shot, the application SHALL display a notification toast with the translated message `"Shot did not start — not recorded"`. The toast is informational only — there is no recovery path; a discarded shot is intentionally not recorded. The toast SHALL auto-dismiss after a fixed timeout (timer is permitted here per the project's UI auto-dismiss carve-out).

#### Scenario: Toast appears when a shot is discarded

- **GIVEN** an aborted shot is discarded by the classifier
- **WHEN** the discard signal fires
- **THEN** a toast SHALL appear on the current top page of the QML stack
- **AND** the toast text SHALL be the translated equivalent of `"Shot did not start — not recorded"`
- **AND** the toast SHALL auto-dismiss after a few seconds with no user action required

### Requirement: Shot snapshots SHALL capture equipment_id and rpm
When a shot is saved, the snapshot SHALL record the active bag's `equipment_id` (the package pointer) and the `rpm` dial-in value, in place of the dropped grinder identity columns. The grind setting SHALL continue to be snapshotted. Grinder brand/model/burrs SHALL NOT be stored on the shot row; they are resolved via `equipment_id` at read time.

#### Scenario: Shot saved with a linked package
- **WHEN** a shot is saved while the active bag has a linked equipment package
- **THEN** the shot row SHALL store that package's `equipment_id`, the grind setting, and the rpm
- **AND** the shot row SHALL NOT store grinder brand/model/burrs strings

#### Scenario: Shot saved with no equipment
- **WHEN** a shot is saved while the active bag has no linked equipment
- **THEN** the shot's `equipment_id` SHALL be null and grinder identity SHALL resolve as empty

### Requirement: Active bag fields written to shot snapshot
Shot save SHALL snapshot the active bag's lifecycle fields (`bagId`, `frozenDate`, `defrostDate`) and populate the `shots.beanbase_id` column directly, in addition to the existing DYE-sourced snapshot fields (`beanBrand`, `beanType`, `beanNotes`, `roastDate`, `roastLevel`, `beanBaseId`, `beanBaseData`).

#### Scenario: Shot saved with frozen bag active
- **WHEN** a shot ends and the active bag has `frozenDate` and `defrostDate` set
- **THEN** the shot record SHALL include `bagId`, `frozenDate`, and `defrostDate`

#### Scenario: Shot saved with no active bag
- **WHEN** a shot ends and `activeBagId` is null
- **THEN** `bagId`, `frozenDate`, and `defrostDate` SHALL all be null in the shot record

### Requirement: Active bag dose/yield stamped on shot save
On shot save with an active bag (`activeBagId` non-null), the system SHALL update the active bag's `doseWeightG` and `yieldTargetG` to the shot's actual values (which may originate from SAW/profile settings rather than a manual edit), on the background DB thread with no user prompt. The `beansModified` divergence mechanism (and `recomputeBeansModified()`) SHALL be removed from `SettingsDye`, since pre-shot grinder edits write through to the active bag directly.

#### Scenario: Dose stamp does not block shot save
- **WHEN** the DB write to update the bag's dose/yield fields fails
- **THEN** the shot SHALL still be saved
- **AND** the bag update failure SHALL be logged but not surfaced to the user

### Requirement: Cleaning/maintenance shots remain excluded
The existing filter that excludes `cleaning`, `descale`, and `calibrate` profile shots from being saved SHALL remain in place and is not modified by this change.

#### Scenario: Maintenance shot excluded
- **WHEN** a shot ends for a profile with `beverageType` of `"cleaning"`, `"descale"`, or `"calibrate"`
- **THEN** no shot record SHALL be created, no bag grinder auto-write SHALL run, and `m_extractionStarted` SHALL be reset to false

