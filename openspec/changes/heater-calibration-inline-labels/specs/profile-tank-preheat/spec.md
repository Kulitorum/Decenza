# profile-tank-preheat Delta Spec

## ADDED Requirements

### Requirement: Profile tank preheat temperature is written on every profile upload
`DE1Device` SHALL write the active profile's `tank_desired_water_temperature` to `DE1::MMR::TANK_TEMP_THRESHOLD` (0x80380C) as part of every profile upload, in both `uploadProfile()` and `uploadProfileAndStartEspresso()`. The value SHALL be rounded to the nearest integer and clamped to 0–45 °C (matching de1app's `range_check_variable` bounds). The write SHALL go through `writeMMR()` so the existing `m_lastMMRValues` dedup cache elides repeat writes of an unchanged value, and so the firmware-flash guard applies.

#### Scenario: Profile with preheat request
- **WHEN** a profile whose `tank_desired_water_temperature` is 35 is uploaded to the DE1
- **THEN** an MMR write of 35 to address 0x80380C is queued alongside the profile header/frames

#### Scenario: Profile without preheat
- **WHEN** a profile whose `tank_desired_water_temperature` is 0 (the default) is uploaded
- **THEN** the tank threshold MMR is written as 0 (or elided by the dedup cache if already 0)

#### Scenario: Out-of-range value is clamped
- **WHEN** a profile carries `tank_desired_water_temperature` of 60
- **THEN** the MMR write is clamped to 45

#### Scenario: Switching back to a no-preheat profile disables preheat
- **WHEN** a profile with preheat 35 is uploaded and then a profile with preheat 0 is uploaded
- **THEN** the second upload writes 0 to the tank threshold MMR, disabling preheat

### Requirement: Connect-time baseline remains 0
`sendInitialSettings()` SHALL continue to write `TANK_TEMP_THRESHOLD = 0` on connect as the pre-profile baseline. The subsequent profile upload (via `MainController::applyAllSettings()`) SHALL establish the profile's actual value.

#### Scenario: Fresh connect then profile upload
- **WHEN** the DE1 connects and the active profile requests preheat 30
- **THEN** the machine first receives threshold 0 during initial settings, then 30 when the profile is uploaded
