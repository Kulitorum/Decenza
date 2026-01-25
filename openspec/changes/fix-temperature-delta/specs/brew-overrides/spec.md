## MODIFIED Requirements

### Requirement: Session-Only Brew Overrides

Temperature overrides SHALL be applied as a delta offset relative to the profile's reference temperature (first frame / `espressoTemperature`). The delta is computed as `override - espressoTemperature` and added to each frame's individual temperature, preserving relative temperature differences between frames.

#### Scenario: User sets temperature override with multi-temp profile
- **WHEN** the profile has frames with temperatures [93, 93, 88, 88] (espressoTemperature = 93)
- **AND** the user sets a brew temperature of 95°C in the BrewDialog
- **THEN** the delta is +2°C (95 - 93)
- **AND** the uploaded profile frames have temperatures [95, 95, 90, 90]
- **AND** the IdlePage displays the override as "93 → 95°C"

#### Scenario: User sets temperature override lower than profile default
- **WHEN** the profile has frames with temperatures [90, 90, 85] (espressoTemperature = 90)
- **AND** the user sets a brew temperature of 88°C in the BrewDialog
- **THEN** the delta is -2°C (88 - 90)
- **AND** the uploaded profile frames have temperatures [88, 88, 83]

## ADDED Requirements

### Requirement: Profile Editor Global Temperature Delta
The Profile Editor's global temperature field ("All temps") SHALL apply temperature changes as a delta offset relative to the current first frame temperature, preserving relative differences between frames. The `espressoTemperature` profile-level field SHALL be updated to the new first frame value.

#### Scenario: Changing global temperature with varying frame temps
- **WHEN** the profile has frames with temperatures [93, 93, 88, 88]
- **AND** the user changes the global temperature from 93 to 95
- **THEN** the delta is +2°C
- **AND** the frames become [95, 95, 90, 90]
- **AND** `espressoTemperature` is set to 95

#### Scenario: Changing global temperature with uniform frame temps
- **WHEN** all frames have the same temperature (e.g., [90, 90, 90])
- **AND** the user changes the global temperature to 92
- **THEN** all frames become [92, 92, 92] (delta and absolute produce same result)
