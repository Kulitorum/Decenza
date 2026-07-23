## MODIFIED Requirements

### Requirement: Brew Dialog

The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL display the current profile name and bean info as a "Base Recipe" header, and allow editing temperature, dose, ratio, yield and grind (in this order) for the next shot. The temperature control SHALL be presented as a temperature **offset** (labeled "Temp Delta:") applied uniformly to the whole profile: it reads `0°` at the active baseline and `+N°`/`-N°` when adjusted. The active baseline SHALL be the active recipe's offset-derived temperature (the profile's `espressoTemperature` + the recipe's stored `tempOffsetC`) when a recipe is active and carries a non-zero offset, otherwise the profile default. Because the control shows an offset rather than an absolute temperature, the dialog SHALL display below it the **resulting** temperature(s) — the profile frames shifted by the dialed offset — rendered adaptively (single value / spaced mid-dot list / first…last ellipsis) and updating live as the offset changes. It SHALL NOT append a signed delta tag (the stepper already carries the offset), and SHALL highlight the sub-indicator when the dialed value deviates from the baseline.

The Clear action SHALL reset each field to its active baseline — the value defined by the override-highlight scheme in `recipe-aware-brew-settings`. For Temp Delta and Stop-at (yield) this baseline is the active recipe's offset-derived temperature / its `yieldG` when a recipe is active, and the profile default otherwise; Dose, Ratio, and grind reset to their existing defaults unchanged. Clear SHALL only strip per-brew deviations from the active baseline; when a recipe is active it SHALL NOT wipe the recipe's own designed yield/temperature back to the profile default.

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

### Requirement: Shot Plan Display
The system SHALL display a summary line showing the configured shot parameters: profile name with temperature, bean name with grind setting, and dose/yield weights. The line SHALL be clickable to open the BrewDialog. Visibility SHALL be controlled by a "Show shot plan" setting (default: enabled). When a "Show on all screens" setting is enabled, the shot plan line SHALL appear in the top status bar on all pages; otherwise it SHALL appear only on the IdlePage.

The temperature portion SHALL render the resulting temperature(s) adaptively based on the number of distinct frame temperatures (N), so that multi-temperature profiles are not misrepresented as a single value:
- **N = 1:** a single value (e.g. `90°C`).
- **N = 2:** both distinct temperatures listed with a spaced mid-dot separator (e.g. `88 · 93°C`).
- **N ≥ 3:** the first-step temperature and last-step temperature joined by an ellipsis, preserving trajectory order rather than sorting (e.g. `84…52°C`).

When a temperature override (or a recipe's offset) is active, the temperature portion SHALL render the **resulting** temperature(s) — every frame shifted by the effective offset — using the same adaptive N=1/2/3 notation, and SHALL highlight the temperature portion (per the plan-widgets per-item override scheme) to mark the deviation from the baseline. It SHALL NOT append a signed delta tag, and SHALL NOT render the override as a from→to arrow. Multi-temperature profiles (N ≥ 2) SHALL render with the list/ellipsis notation even when no override is active.

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
