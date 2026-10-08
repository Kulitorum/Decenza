# brew-overrides Specification

## Purpose
Defines the persistent per-brew overrides (temperature, dose, ratio, yield, grind) set via the BrewDialog: temperature is stored and applied as a delta relative to the profile's `espressoTemperature` anchor, identically across the live-brew upload and "Update Profile" save paths; overrides survive app restart, are shown in the shot plan display, and are recorded to and retrievable from shot history.

## Requirements

### Requirement: Persistent Brew Overrides
Temperature overrides SHALL be applied as a delta offset relative to the profile's reference temperature `espressoTemperature`: the delta `override - espressoTemperature` SHALL be added to each frame's temperature, preserving relative differences between frames. Every path that applies a temperature override as a delta, including the live-brew upload and the Brew Dialog "Update Profile" path, SHALL use the same anchor.

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

### Requirement: Temperature overrides survive restarts and are stored per shot
Temperature overrides SHALL persist across app restarts in QSettings. Shot history SHALL store them in dedicated `temperature_override` database columns.

#### Scenario: Temperature override is stored on the shot
- **WHEN** a shot is saved while a temperature override is active
- **THEN** its `temperature_override` column records the override value

### Requirement: Brew Dialog
The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL show the profile name and bean info as a "Base Recipe" header and allow editing temperature, dose, ratio, yield and grind, in that order, for the next shot.

#### Scenario: Opening the BrewDialog
- **WHEN** the user taps the shot plan text on IdlePage
- **THEN** the BrewDialog opens with current values populated from Settings (DYE metadata and profile defaults)
- **AND** the targetWeight and targetTemperature are set with a precedence order: overrides first, then profile defaults

#### Scenario: Temperature offset control
- **WHEN** the BrewDialog opens with no recipe active and no temperature override active
- **THEN** the "Temp Delta:" control reads `0°`
- **AND** the resulting temperature(s) are shown below it using the adaptive notation: a single value for a one-temperature profile, a spaced mid-dot list for two distinct temperatures (e.g. "Profile: 90 · 88°C"), or first-step…last-step ellipsis for three or more (at offset `0°` the resulting values equal the profile's own temperatures)
- **WHEN** the user adjusts the control to `+2°`
- **THEN** every frame's temperature is raised by 2°C for the next shot, and the temperature(s) shown below shift to the resulting values (each 2°C higher), highlighted to mark the deviation — with no signed delta tag
- **AND** a "Update Profile" action permanently bakes the `+2°` offset into every frame

#### Scenario: Temperature offset anchored on the active recipe
- **WHEN** the BrewDialog opens with a recipe active whose `tempOffsetC` is −4
- **THEN** the "Temp Delta:" control reads `0°` (the dial sits on the recipe's design temperature, not a deviation)
- **WHEN** the user adjusts the control to `+1°`
- **THEN** the next shot brews 1°C above the recipe's design temperature

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

#### Scenario: Clear all overrides with no recipe active
- **WHEN** no recipe is active and the user taps the "Clear" button in the BrewDialog
- **THEN** all fields reset to profile defaults (temperature) and empty/default values (dose=18g, grind=bean"", ratio=calculated from profile target weight / 18g)

#### Scenario: Clear returns to the recipe baseline in recipe mode
- **WHEN** a recipe with `yieldG` = 36 and `tempOffsetC` = −4 is active, the user has dialed a per-brew deviation (e.g. Stop-at 40, Temp Delta +2°), and the user taps "Clear"
- **THEN** Stop-at returns to 36 and the Temp Delta returns to `0°` (the recipe's temperature)
- **AND** the recipe's stored `yieldG` / `tempOffsetC` are unchanged (Clear does not edit the recipe)

### Requirement: Temperature control is an offset from the active baseline
The temperature control SHALL be a uniform offset labelled "Temp Delta:" applied to the whole profile, reading `0°` at the active baseline and `+N°` or `-N°` when adjusted. The active baseline SHALL be the active recipe's offset-derived temperature when a recipe is active and carries a non-zero offset, otherwise the profile default.

#### Scenario: Recipe offset sets the baseline
- **WHEN** a recipe with a non-zero `tempOffsetC` is active and the dialog opens
- **THEN** Temp Delta reads `0°` at the recipe-derived temperature

### Requirement: The dialog shows the resulting temperatures
Below the control the dialog SHALL display the **resulting** temperature(s), the profile frames shifted by the dialed offset, rendered adaptively as a single value, a spaced mid-dot list, or a first-to-last ellipsis, and updating live. It SHALL NOT append a signed delta tag, and SHALL highlight the sub-indicator when the dialed value deviates from the baseline.

#### Scenario: Resulting temperatures update with the offset
- **WHEN** the user adjusts Temp Delta
- **THEN** the resulting temperature(s) update live using the adaptive notation

### Requirement: Clear resets to the active baseline
The Clear action SHALL reset each field to its active baseline, as defined in `recipe-aware-brew-settings`. For Temp Delta and Stop-at (yield) this is the active recipe's offset-derived temperature and `yieldG` when a recipe is active, otherwise the profile default. Dose, Ratio and grind SHALL reset to their existing defaults. Clear SHALL NOT wipe a recipe's designed yield or temperature back to the profile default.

#### Scenario: Clear keeps the recipe's designed yield
- **WHEN** the user taps Clear while a recipe is active
- **THEN** Stop-at returns to the recipe's `yieldG`, not the profile default

### Requirement: Shot Plan Display
The system SHALL display a summary line of the configured shot parameters: profile name with temperature, bean name with grind setting, and dose/yield weights. The line SHALL open the BrewDialog when clicked. It SHALL be controlled by a "Show shot plan" setting, default enabled. With "Show on all screens" enabled it SHALL appear in the top status bar on all pages; otherwise only on the IdlePage.

#### Scenario: Shot plan with single-temperature profile, no overrides
- **WHEN** no overrides are active, the profile has one distinct frame temperature, and DYE metadata is populated
- **THEN** the shot plan shows: "ProfileName (88°C) · BeanName (grind) · 18.0g in, 36.0g out"

#### Scenario: Shot plan with single-temperature profile and temperature override
- **WHEN** a temperature override of +2°C is active and the profile has one distinct frame temperature of 88°C
- **THEN** the temperature portion shows the resulting value, highlighted: "ProfileName (90°C)" with the temperature in the override-highlight color

#### Scenario: Shot plan with two distinct temperatures
- **WHEN** the profile has two distinct frame temperatures (e.g. 88 and 93)
- **THEN** with no override the temperature portion shows the mid-dot list: "ProfileName (88 · 93°C)"
- **AND** with a +2° override active it shows the resulting list, highlighted: "ProfileName (90 · 95°C)"

#### Scenario: Shot plan with three or more distinct temperatures
- **WHEN** the profile has three or more distinct frame temperatures with first-step 84°C and last-step 52°C
- **THEN** with no override the temperature portion shows the ellipsis notation: "ProfileName (84…52°C)"
- **AND** with a +1° override active it shows the resulting ellipsis, highlighted: "ProfileName (85…53°C)"

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

### Requirement: The shot plan temperature adapts to distinct frame temperatures
The temperature portion SHALL render by the number N of distinct frame temperatures. N = 1 SHALL show a single value. N = 2 SHALL show both values with a spaced mid-dot separator. N of 3 or more SHALL show the first-step and last-step temperatures joined by an ellipsis, in trajectory order rather than sorted. Multi-temperature profiles (N of 2 or more) SHALL use this notation even when no override is active.

#### Scenario: Multi-temperature profile without an override
- **WHEN** a profile has two or more distinct frame temperatures and no override is active
- **THEN** the temperature portion uses the list or ellipsis notation

### Requirement: An active override shows the resulting temperatures
When a temperature override or a recipe offset is active, the temperature portion SHALL render the **resulting** temperature(s), with every frame shifted by the effective offset, using the same notation. It SHALL highlight the temperature portion to mark the deviation from baseline. It SHALL NOT append a signed delta tag, and SHALL NOT render the override as a from-to arrow.

#### Scenario: Override shifts every displayed frame temperature
- **WHEN** a temperature override is active on a multi-temperature profile
- **THEN** each displayed temperature is shifted by the effective offset and the portion is highlighted

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
The system SHALL store temperature and yield overrides in QSettings for persistence across app sessions. The yield override SHALL be stored as a `YieldSpec`, a value plus a `none`, `absolute` or `ratio` mode (`yield-anchor`), not as a bare gram number.

#### Scenario: Overrides persist between app sessions
- **WHEN** the user sets temperature or yield overrides in the BrewDialog
- **THEN** the values are immediately saved to QSettings
- **AND** when the app is restarted, the overrides are restored from QSettings — including the yield's mode
- **AND** the overrides remain active until explicitly cleared

#### Scenario: An absolute yield override clears on a profile switch
- **WHEN** the session anchor is `{40.0, absolute}`, neither an active recipe nor the active bag saves a yield, and the user switches to a different profile
- **THEN** the yield override is cleared from QSettings and `hasBrewYieldOverride` becomes false
- **AND** the IdlePage shot plan returns to the new profile's target weight with no highlight

#### Scenario: A ratio yield override survives a profile switch
- **WHEN** the session anchor is `{2.0, ratio}` and the user switches to a different espresso profile
- **THEN** the anchor remains `{2.0, ratio}` and `hasBrewYieldOverride` stays true
- **AND** the target re-derives against the current dose on the new profile

#### Scenario: A ratio yield override clears when the beverage group changes
- **WHEN** the session anchor is `{2.5, ratio}`, neither an active recipe nor the active bag saves a yield, and the user switches from an espresso profile to a tea profile
- **THEN** `hasBrewYieldOverride` becomes false and the stop-at-weight target is the tea profile's own `target_weight`

#### Scenario: The bean's saved yield applies after a switch
- **WHEN** the active bag saves `{2.0, ratio}`, no recipe is active, and a profile switch leaves no yield override
- **THEN** the anchor is `{2.0, ratio}` and `hasBrewYieldOverride` is true

#### Scenario: Temperature still clears on a profile switch
- **WHEN** a temperature override is active and the user switches profiles
- **THEN** `hasTemperatureOverride` becomes false

#### Scenario: Returning from a cleaning run keeps the temperature override
- **WHEN** a temperature override is active on a profile, and the user loads a cleaning profile and then that profile again
- **THEN** the cleaning run brews at its own temperature, and the returning profile brews at the override

#### Scenario: Overrides cleared via BrewDialog
- **WHEN** the user taps "Clear" in the BrewDialog
- **THEN** all overrides are removed from QSettings and the override flags become false
- **AND** the Settings properties are reset to default values

#### Scenario: Overrides cleared on recipe activation
- **WHEN** a recipe is activated
- **THEN** the override flags reflect only that recipe's own stored overrides (or false, if it has none) — not a leftover flag from whatever was active before

#### Scenario: A ratio deriving the profile's own target still reads as active
- **WHEN** the session anchor is `{2.0, ratio}`, the dose is 18 g, and the active profile's `target_weight` is 36 g
- **THEN** `hasBrewYieldOverride` is true and `brewByRatioActive` is true
- **AND** a subsequent dose change still re-derives the target

#### Scenario: Deactivation alone leaves the live setup untouched
- **WHEN** the active recipe is deactivated without switching profile or bag
- **THEN** the live brew values — including any override the recipe applied — remain in effect (deactivation drops the recipe association, not the dialed setup); the next profile or recipe switch clears them as above

#### Scenario: A frozen shot value matching the current profile is not flagged
- **WHEN** a shot or favorite is loaded whose saved `temperatureOverride` happens to equal the freshly-loaded profile's own default temperature
- **THEN** `hasTemperatureOverride` is false and the Shot Plan shows no highlight

### Requirement: Active flags reflect deliberate overrides only
Active state SHALL be tracked by genuine boolean flags (`hasTemperatureOverride`, `hasBrewYieldOverride`) that reflect whether a deliberate override is in effect, not whether a value was ever set. For the yield, active SHALL mean `mode != none` and SHALL NEVER be inferred by comparing a resolved gram value against the profile's target weight.

#### Scenario: Ratio deriving the profile target is still active
- **WHEN** a ratio anchor derives exactly the profile's target weight
- **THEN** the yield override still reads as active

### Requirement: Overrides are cleared by flag, not by resync
Overrides SHALL be cleared, with the flag set false rather than the value resynced to a new default, when a recipe is activated (before its own overrides apply) or when the user taps "Clear" in the BrewDialog.

#### Scenario: Recipe activation clears the flags
- **WHEN** a recipe is activated
- **THEN** the override flags are set false before the recipe's own overrides apply

### Requirement: Profile switches clear overrides by mode
On a profile switch the yield override SHALL clear when its mode is `absolute`, or when it is `ratio` and the beverage group changes. Where no yield override remains, a saved yield SHALL be armed as `yield-anchor` specifies. The temperature override SHALL clear on a profile switch, except when returning from a maintenance run to the drink profile loaded before it, which SHALL keep every override. A maintenance profile clears nothing.

#### Scenario: Maintenance run does not clear overrides
- **WHEN** the user starts a maintenance profile and then returns to the drink profile loaded before it
- **THEN** every override is kept

### Requirement: Frozen override values count as active only when they differ
Loading a shot or favorite that carries its own frozen override value SHALL mark the flag active only when that value genuinely differs from the freshly-loaded profile's own default, using the same threshold as the Shot Plan display.

#### Scenario: Frozen value matching the profile is not active
- **WHEN** a loaded shot's frozen override equals the current profile's default
- **THEN** the override flag stays false

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

### Requirement: The ratio widget sets the session anchor
The Ratio quick-select widget and its preset dialog SHALL write a **ratio anchor** to the session, identical in effect to editing the ratio control in Brew Settings, rather than flattening `dose × ratio` into an absolute yield. Picking a ratio SHALL NOT write to any recipe or bag; it arms the session only, and persisting it remains the Update button's job in Brew Settings.

#### Scenario: Tapping a ratio preset arms a ratio anchor
- **WHEN** the user taps the 1:2 preset with an 18 g dose
- **THEN** the session anchor becomes `{2.0, ratio}` and the target derives to 36 g
- **AND** a later dose change to 17.5 g re-derives the target to 35 g

#### Scenario: The widget does not write the recipe or bag
- **WHEN** a recipe holding `{36.0, absolute}` is active and the user taps the 1:2 preset
- **THEN** the session anchor becomes `{2.0, ratio}` and reads as an override against the recipe
- **AND** the recipe still holds `{36.0, absolute}`

#### Scenario: The widget shows override state against the active anchor
- **WHEN** the session anchor deviates from the active recipe's or bag's stored spec
- **THEN** the ratio widget SHALL render in the override-highlight color, consistent with the Brew Settings rows

#### Scenario: No yield is ever derived from lastUsedRatio
- **WHEN** any dose capture, recipe activation, or bag selection occurs
- **THEN** no code path SHALL compute a yield as `dose × lastUsedRatio`

### Requirement: lastUsedRatio is preset memory only
`Settings.brew.lastUsedRatio` SHALL be preset memory only: which preset is highlighted in the picker, and the seed for a fresh brew with no recipe or bag anchor. It SHALL NOT be read to derive any yield.

#### Scenario: Fresh brew seeds from lastUsedRatio
- **WHEN** a brew starts with no recipe or bag anchor
- **THEN** the ratio is seeded from `lastUsedRatio` and no yield is derived from it
