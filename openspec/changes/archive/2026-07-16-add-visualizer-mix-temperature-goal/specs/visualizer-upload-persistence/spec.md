## ADDED Requirements

### Requirement: Shot sampling SHALL capture the machine's mix temperature setpoint

The DE1 `ShotSample` notification carries two temperature setpoints: `SetMixTemp` (the target for water entering the group) and `SetHeadTemp` (the target for the basket). Decenza SHALL decode both and retain them as distinct per-sample series for the duration of a shot.

#### Scenario: New BLE spec packet decodes both setpoints
- **WHEN** a 19-byte `ShotSample` notification is received
- **THEN** `SetMixTemp` SHALL be decoded from bytes 11–12 as an unsigned big-endian short divided by 256
- **AND** `SetHeadTemp` SHALL continue to be decoded from bytes 13–14
- **AND** the two values SHALL be exposed as separate fields, neither overwriting the other

#### Scenario: Old BLE spec packet decodes both setpoints
- **WHEN** a 17-byte `ShotSample` notification is received
- **THEN** `SetMixTemp` SHALL be decoded from bytes 8–9
- **AND** `SetHeadTemp` SHALL continue to be decoded from bytes 10–11

#### Scenario: Existing temperature goal semantics are preserved
- **WHEN** any `ShotSample` is decoded
- **THEN** the field feeding `temperature.goal` on upload SHALL remain `SetHeadTemp`, matching de1app's `espresso_temperature_goal` and Visualizer's "Basket Temperature Goal" series

### Requirement: The mix temperature goal SHALL persist with the shot

The mix temperature goal series SHALL survive a save/load round-trip so that re-uploads, queued uploads, and the shot detail graph all see the same data the live shot did.

#### Scenario: Series round-trips through storage
- **WHEN** a shot with mix goal samples is saved and then loaded from shot history
- **THEN** the loaded record SHALL carry a mix goal series equal to the one recorded

#### Scenario: Shots recorded before this change load without error
- **WHEN** a shot whose stored sample blob has no mix goal key is loaded
- **THEN** the load SHALL succeed
- **AND** the mix goal series SHALL be empty
- **AND** no other series SHALL be affected

### Requirement: Visualizer upload SHALL include the mix temperature goal when available

Uploaded shot JSON SHALL carry `temperature.mix_goal` alongside `temperature.goal`, interpolated onto the elapsed timeline using the same interpolation applied to every other goal series. Both the live upload path and the history/re-upload path SHALL produce it.

#### Scenario: Live upload includes mix_goal
- **WHEN** a shot recorded with mix goal data is uploaded to Visualizer
- **THEN** `temperature.mix_goal` SHALL be present
- **AND** it SHALL have the same length as `elapsed`
- **AND** `temperature.goal` SHALL still carry the basket (`SetHeadTemp`) goal

#### Scenario: Re-upload from history includes mix_goal
- **WHEN** a stored shot carrying mix goal data is re-uploaded from shot history
- **THEN** its JSON SHALL contain `temperature.mix_goal` with the same values the live upload would have sent

#### Scenario: Shots without mix goal data omit the key entirely
- **WHEN** a shot with an empty mix goal series (recorded before this change, or imported from a `.shot` file) is uploaded
- **THEN** `temperature.mix_goal` SHALL be absent from the JSON
- **AND** the upload SHALL NOT send a zero-filled array

#### Scenario: History upload path matches the live path for mix temperature
- **WHEN** a stored shot carrying measured mix temperature data is re-uploaded from history
- **THEN** `temperature.mix` SHALL be present, as it is on the live upload path
