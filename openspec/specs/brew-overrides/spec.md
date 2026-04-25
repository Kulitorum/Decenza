# brew-overrides Specification

## Purpose
TBD - created by archiving change add-brew-overrides. Update Purpose after archive.
## Requirements
### Requirement: Persistent Brew Overrides

Temperature overrides SHALL be applied as a delta offset relative to the profile's reference temperature (first frame / `espressoTemperature`). The delta is computed as `override - espressoTemperature` and added to each frame's individual temperature, preserving relative temperature differences between frames.

**Storage:** Temperature overrides are persistent (stored in QSettings) and survive app restarts. They are stored in shot history as dedicated `temperature_override` database columns.

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

### Requirement: Brew Dialog
The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL display the current profile name and bean info as a "Base Recipe" header, and allow editing temperature, dose, ratio, yield and grind (in this order) for the next shot.

#### Scenario: Opening the BrewDialog
- **WHEN** the user taps the shot plan text on IdlePage
- **THEN** the BrewDialog opens with current values populated from Settings (DYE metadata and profile defaults)
- **AND** the targetWeight and targetTemperature are set with a precedence order: overrides first, then profile defaults

#### Scenario: Temperature override with save option
- **WHEN** the user changes the temperature in the BrewDialog
- **THEN** the dialog shows the profile default temperature below the input
- **AND** a "Save" button allows permanently updating the profile temperature

#### Scenario: Dose from scale
- **WHEN** the user taps "Get from scale" in the BrewDialog
- **AND** the scale reports weight ≥ 3g
- **THEN** the dose value is updated to the scale reading
- **WHEN** the scale reports weight < 3g
- **THEN** a warning is shown asking the user to place the portafilter on the scale

#### Scenario: Ratio and yield auto-calculation
- **WHEN** the user changes dose or ratio
- **THEN** yield is recalculated automatically (dose × ratio)
- **WHEN** the user manually edits the yield value
- **THEN** the ratio is changed automatically (yield / dose)

#### Scenario: Clear all overrides
- **WHEN** the user taps the "Clear" button in the BrewDialog
- **THEN** all fields reset to profile defaults (temperature) and empty/default values (dose=18g, grind=bean"", ratio=calculated from profiel target weight / 18g)

### Requirement: Shot Plan Display
The system SHALL display a summary line showing the configured shot parameters: profile name with temperature, bean name with grind setting, and dose/yield weights. The line SHALL be clickable to open the BrewDialog. Visibility SHALL be controlled by a "Show shot plan" setting (default: enabled). When a "Show on all screens" setting is enabled, the shot plan line SHALL appear in the top status bar on all pages; otherwise it SHALL appear only on the IdlePage.

#### Scenario: Shot plan with no overrides
- **WHEN** no overrides are active and DYE metadata is populated
- **THEN** the shot plan shows: "ProfileName (88°C) · BeanName (grind) · 18.0g in, 36.0g out"

#### Scenario: Shot plan with temperature override
- **WHEN** a temperature override is active
- **THEN** the temperature portion shows the arrow notation: "ProfileName (88 → 90°C)"

#### Scenario: Shot plan hidden when empty
- **WHEN** no profile is loaded and no DYE metadata is set
- **THEN** the shot plan line is not visible

#### Scenario: Shot plan disabled via settings
- **WHEN** the "Show shot plan" setting is disabled
- **THEN** the shot plan line is not visible on any page

#### Scenario: Shot plan on idle page only (default)
- **WHEN** "Show shot plan" is enabled and "Show on all screens" is disabled
- **THEN** the shot plan line appears only on the IdlePage within the page content

#### Scenario: Shot plan on all screens
- **WHEN** "Show shot plan" is enabled and "Show on all screens" is enabled
- **THEN** the shot plan line appears in the top status bar between the page title and the indicators
- **AND** tapping it opens the BrewDialog from any page

### Requirement: Brew Overrides History Recording
The system SHALL record the active brew overrides (temperature, yield) as dedicated database columns in the shot history when a shot is saved. This enables traceability of per-shot adjustments.

#### Scenario: Overrides saved to shot history
- **WHEN** a shot ends with active brew overrides
- **THEN** the temperature override is stored in the `temperature_override` column (NULL if not set)
- **AND** the yield override is stored in the `yield_override` column (NULL if not set)
- **AND** the overrides are available when viewing the shot in history

#### Scenario: No overrides recorded when none active
- **WHEN** a shot ends without any active brew overrides
- **THEN** the `temperature_override` and `yield_override` columns are NULL

### Requirement: Shot History Parameter Retrieval
The system SHALL populate brew parameters (dose, yield, grind) from shot history when a shot is loaded via `loadShotWithMetadata()`. This allows the user to repeat a previous shot's settings. If the shot has recorded brew overrides, those take precedence; otherwise the profile's target weight is used for yield.

#### Scenario: Loading shot with brew overrides from history
- **WHEN** the user loads a shot from history that has override columns populated
- **THEN** the dose override is set from the DYE metadata (shot-specific, not override)
- **AND** the yield override is set from the `yield_override` column if not NULL
- **AND** the temperature override is set from the `temperature_override` column if not NULL
- **AND** the grinder setting is populated from the DYE metadata
- **AND** the BrewDialog shows these as active overrides

#### Scenario: Loading shot without brew overrides from history
- **WHEN** the user loads a shot from history that has NULL override columns
- **THEN** no temperature or yield overrides are set
- **AND** the yield defaults to the loaded profile's target weight
- **AND** the dose defaults to the DYE bean weight (default 18g)

#### Scenario: BrewDialog pre-populated from history
- **WHEN** the BrewDialog opens after loading a shot from history
- **THEN** dose, yield, and grind fields reflect the active overrides (if set) or profile defaults
- **AND** the ratio is calculated from the effective dose and yield

### Requirement: Persistent Override Storage
The system SHALL store temperature and yield overrides in QSettings for persistence across app sessions. Overrides SHALL be cleared when switching profiles or when the user taps "Clear" in the BrewDialog.

#### Scenario: Overrides persist between app sessions
- **WHEN** the user sets temperature or yield overrides in the BrewDialog
- **THEN** the values are immediately saved to QSettings
- **AND** when the app is restarted, the overrides are restored from QSettings
- **AND** the overrides remain active until explicitly cleared

#### Scenario: Overrides cleared on profile switch
- **WHEN** the user switches to a different profile
- **THEN** all overrides are cleared from QSettings
- **AND** the IdlePage shot plan returns to profile defaults

#### Scenario: Overrides cleared via BrewDialog
- **WHEN** the user taps "Clear" in the BrewDialog
- **THEN** all overrides are removed from QSettings
- **AND** the Settings properties are reset to default values

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

