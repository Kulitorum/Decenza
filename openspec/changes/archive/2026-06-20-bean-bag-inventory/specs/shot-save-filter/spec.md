## ADDED Requirements

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
