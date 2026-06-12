## MODIFIED Requirements (delta from existing shot-save behaviour)

### Requirement: Active bag fields written to shot snapshot (modification)
This spec modifies the existing shot save flow to include bag lifecycle fields in the shot snapshot.

#### Before this change
- Shot save snapshots: `beanBrand`, `beanType`, `beanNotes`, `roastDate`, `roastLevel`, `beanBaseId`, `beanBaseData` (from DYE settings).
- No `frozenDate`, `defrostDate`, or `bagId` in the snapshot.

#### After this change
- Shot save SHALL additionally snapshot from the active bag:
  - `bagId` (int — the local DB id of the bag, nullable)
  - `frozenDate` (nullable date string)
  - `defrostDate` (nullable date string)
- Shot save SHALL also populate the new `shots.beanbase_id` column directly (canonical UUID, nullable) alongside the existing `beanbase_json` blob.
- All other snapshot fields are unchanged.

#### Scenario: Shot saved with frozen bag active
- **WHEN** a shot ends and the active bag has `frozenDate` and `defrostDate` set
- **THEN** the shot record SHALL include `bagId`, `frozenDate`, and `defrostDate`

#### Scenario: Shot saved with no active bag
- **WHEN** a shot ends and `activeBagId` is null
- **THEN** `bagId`, `frozenDate`, and `defrostDate` SHALL all be null in the shot record

### Requirement: Active bag dose/yield stamped on shot save (modification)
This spec modifies the existing shot save flow to eliminate the `beansModified` divergence mechanism and stamp shot values back to the active bag.

#### Before this change
- `beansModified` is a computed read-only property on `SettingsDye` — true when the live DYE fields diverge from the selected preset across 10 monitored fields — driving save-to-preset prompts in the UI.

#### After this change
- Grinder fields are write-through: pre-shot edits write directly to the active bag (see `coffee-bag-model` spec), so there is no divergence to detect.
- **WHEN** a shot is saved and an active bag is set (`activeBagId` is non-null)
- **THEN** the system SHALL update the active bag's `doseWeightG` and `yieldTargetG` to the shot's actual values (these may originate from SAW/profile settings rather than a manual edit)
- **AND** this update SHALL happen on the background DB thread with no user prompt
- **AND** the `beansModified` computed property and `recomputeBeansModified()` SHALL be removed from `SettingsDye`

#### Scenario: Dose stamp does not block shot save
- **WHEN** the DB write to update the bag's dose/yield fields fails
- **THEN** the shot SHALL still be saved
- **AND** the bag update failure SHALL be logged but not surfaced to the user

### Requirement: Cleaning/maintenance shots remain excluded
The existing filter that excludes `cleaning`, `descale`, and `calibrate` profile shots from being saved SHALL remain in place and is not modified by this change.

#### Scenario: Maintenance shot excluded
- **WHEN** a shot ends for a profile with `beverageType` of `"cleaning"`, `"descale"`, or `"calibrate"`
- **THEN** no shot record SHALL be created, no bag grinder auto-write SHALL run, and `m_extractionStarted` SHALL be reset to false
