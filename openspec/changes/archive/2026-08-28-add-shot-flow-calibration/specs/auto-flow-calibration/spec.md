## ADDED Requirements

### Requirement: The flow calibration multiplier a shot ran under is recorded on the shot
The system SHALL record, on each saved shot, the effective flow calibration multiplier that was in
force while that shot was pulled — the same value `effectiveFlowCalibration()` resolves for the
shot's profile and that is written to the machine. The recorded value SHALL be the multiplier in
force at shot START, so an auto-calibration update computed from the shot itself never overwrites
the value the shot is recorded under.

#### Scenario: Shot pulled at a per-profile multiplier
- **WHEN** a shot completes on a profile that has a per-profile flow calibration multiplier and auto
  flow calibration is enabled
- **THEN** the saved shot records that per-profile multiplier

#### Scenario: Shot pulled at the global multiplier
- **WHEN** a shot completes on a profile with no per-profile multiplier, or with auto flow
  calibration disabled
- **THEN** the saved shot records the global flow calibration multiplier

#### Scenario: Auto-calibration updates the multiplier on the same shot
- **WHEN** a shot completes and the auto-calibration batch for its profile completes on that shot,
  writing a new per-profile multiplier before the shot is saved
- **THEN** the saved shot records the multiplier that was in force during the pour, not the newly
  written one

### Requirement: An unrecorded multiplier is distinguishable from a recorded one
The system SHALL represent "no multiplier recorded" as an absent value, never as the neutral
multiplier 1.0, and SHALL NOT infer a multiplier for a shot that has none. Shots saved before this
capability existed, and shots imported from a source that carries no multiplier, SHALL read as
unrecorded.

#### Scenario: Shot saved before the field existed
- **WHEN** a shot recorded before this capability is loaded
- **THEN** its flow calibration multiplier reads as unrecorded, and no value is substituted

#### Scenario: Consumer reads a shot with no recorded multiplier
- **WHEN** a shot with no recorded multiplier is projected for a consumer (MCP tool, AI payload,
  export)
- **THEN** the field is omitted rather than emitted as 0 or 1.0

### Requirement: The recorded multiplier survives transfer and import
The system SHALL carry a shot's recorded flow calibration multiplier through device-to-device
transfer and through shot import, and SHALL leave it unrecorded when the source has no such value
rather than substituting one.

#### Scenario: Device-to-device transfer
- **WHEN** shots are transferred from a device whose database records the multiplier
- **THEN** each transferred shot keeps its recorded multiplier

#### Scenario: Transfer from a source predating the field
- **WHEN** shots are transferred from a database that has no flow calibration column
- **THEN** the imported shots read as unrecorded
