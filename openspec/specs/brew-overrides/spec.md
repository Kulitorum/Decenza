# brew-overrides Specification

## Purpose
TBD - created by archiving change add-brew-overrides. Update Purpose after archive.
## Requirements
### Requirement: Persistent Brew Overrides

Temperature overrides SHALL be applied as a delta offset relative to the profile's reference temperature, defined as the profile scalar `espressoTemperature`. The delta is computed as `override - espressoTemperature` and added to each frame's individual temperature, preserving relative temperature differences between frames. The same anchor (`espressoTemperature`) SHALL be used by every path that applies a temperature override as a delta — both the live-brew upload path and the Brew Dialog "Update Profile" (save-to-profile) path — so that a given override value produces an identical result whether it is brewed or saved.

**Storage:** Temperature overrides are persistent (stored in QSettings) and survive app restarts. They are stored in shot history as dedicated `temperature_override` database columns.

#### Scenario: User sets temperature override with multi-temp profile
- **WHEN** the profile has frames with temperatures [93, 93, 88, 88] (espressoTemperature = 93)
- **AND** the user sets a brew temperature of 95°C in the BrewDialog
- **THEN** the delta is +2°C (95 - 93)
- **AND** the uploaded profile frames have temperatures [95, 95, 90, 90]
- **AND** the shot plan displays the override using the adaptive multi-temperature notation (see Shot Plan Display)

#### Scenario: User sets temperature override lower than profile default
- **WHEN** the profile has frames with temperatures [90, 90, 85] (espressoTemperature = 90)
- **AND** the user sets a brew temperature of 88°C in the BrewDialog
- **THEN** the delta is -2°C (88 - 90)
- **AND** the uploaded profile frames have temperatures [88, 88, 83]

#### Scenario: Live-brew and save paths use the same anchor
- **WHEN** a profile's `espressoTemperature` differs from its first frame temperature
- **AND** the user sets a brew temperature override of T°C
- **THEN** the delta applied by the live-brew upload path equals the delta applied by the "Update Profile" save path (both computed as `T - espressoTemperature`)
- **AND** the temperatures previewed in the Brew Dialog match the temperatures that are brewed and the temperatures that are saved

### Requirement: Brew Dialog
The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL display the current profile name and bean info as a "Base Recipe" header, and allow editing temperature, dose, ratio, yield and grind (in this order) for the next shot. The temperature control SHALL be presented as a temperature **offset** (labeled "Temp Delta:") applied uniformly to the whole profile: it reads `0°` at the profile default and `+N°`/`-N°` when adjusted. Because the control no longer shows an absolute temperature, the dialog SHALL display the profile's actual temperature(s) below it, rendered adaptively (single value / spaced mid-dot list / first…last ellipsis), so the user can see what the offset is applied to.

#### Scenario: Opening the BrewDialog
- **WHEN** the user taps the shot plan text on IdlePage
- **THEN** the BrewDialog opens with current values populated from Settings (DYE metadata and profile defaults)
- **AND** the targetWeight and targetTemperature are set with a precedence order: overrides first, then profile defaults

#### Scenario: Temperature offset control
- **WHEN** the BrewDialog opens with no temperature override active
- **THEN** the "Temp Delta:" control reads `0°`
- **AND** the profile's actual temperature(s) are shown below it using the adaptive notation: a single value for a one-temperature profile, a spaced mid-dot list for two distinct temperatures (e.g. "Profile: 90 · 88°C"), or first-step…last-step ellipsis for three or more
- **WHEN** the user adjusts the control to `+2°`
- **THEN** every frame's temperature is raised by 2°C for the next shot (the profile structure shown below is unchanged)
- **AND** a "Update Profile" action permanently bakes the `+2°` offset into every frame

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

The temperature portion SHALL render the profile's own temperature(s) adaptively based on the number of distinct frame temperatures (N), so that multi-temperature profiles are not misrepresented as a single value:
- **N = 1:** a single value (e.g. `90°C`).
- **N = 2:** both distinct temperatures listed with a spaced mid-dot separator (e.g. `88 · 93°C`).
- **N ≥ 3:** the first-step temperature and last-step temperature joined by an ellipsis, preserving trajectory order rather than sorting (e.g. `84…52°C`).

When a temperature override is active, the profile's temperatures SHALL still be shown (unshifted) followed by a signed delta tag expressing the offset applied to every step (e.g. `90°C +1°`, `88 · 93°C +2°`, `84…52°C -1°`). The override SHALL NOT be rendered as a recomputed single temperature or a from→to arrow. Multi-temperature profiles (N ≥ 2) SHALL render with the list/ellipsis notation even when no override is active.

#### Scenario: Shot plan with single-temperature profile, no overrides
- **WHEN** no overrides are active, the profile has one distinct frame temperature, and DYE metadata is populated
- **THEN** the shot plan shows: "ProfileName (88°C) · BeanName (grind) · 18.0g in, 36.0g out"

#### Scenario: Shot plan with single-temperature profile and temperature override
- **WHEN** a temperature override of +2°C is active and the profile has one distinct frame temperature of 88°C
- **THEN** the temperature portion shows the value and offset: "ProfileName (88°C +2°)"

#### Scenario: Shot plan with two distinct temperatures
- **WHEN** the profile has two distinct frame temperatures (e.g. 88 and 93)
- **THEN** with no override the temperature portion shows the mid-dot list: "ProfileName (88 · 93°C)"
- **AND** with a +2° override active it shows the list and offset: "ProfileName (88 · 93°C +2°)"

#### Scenario: Shot plan with three or more distinct temperatures
- **WHEN** the profile has three or more distinct frame temperatures with first-step 84°C and last-step 52°C
- **THEN** with no override the temperature portion shows the ellipsis notation: "ProfileName (84…52°C)"
- **AND** with a +1° override active it shows the ellipsis and offset: "ProfileName (84…52°C +1°)"

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

