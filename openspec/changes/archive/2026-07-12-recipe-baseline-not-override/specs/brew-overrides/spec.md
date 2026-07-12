## MODIFIED Requirements

### Requirement: Brew Dialog

The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL display the current profile name and bean info as a "Base Recipe" header, and allow editing temperature, dose, ratio, yield and grind (in this order) for the next shot. The temperature control SHALL be presented as a temperature **offset** (labeled "Temp Delta:") applied uniformly to the whole profile: it reads `0°` at the active baseline and `+N°`/`-N°` when adjusted. The active baseline SHALL be the active recipe's own temperature (`tempOverrideC`) when a recipe is active and carries one, otherwise the profile default. Because the control no longer shows an absolute temperature, the dialog SHALL display the temperature(s) the offset is applied to below it, rendered adaptively (single value / spaced mid-dot list / first…last ellipsis).

The Clear action SHALL reset each field to its active baseline — the value defined by the override-highlight scheme in `recipe-aware-brew-settings`. For Temp Delta and Stop-at (yield) this baseline is the active recipe's `tempOverrideC` / `yieldG` when a recipe is active, and the profile default otherwise; Dose, Ratio, and grind reset to their existing defaults unchanged. Clear SHALL only strip per-brew deviations from the active baseline; when a recipe is active it SHALL NOT wipe the recipe's own designed yield/temperature back to the profile default.

#### Scenario: Opening the BrewDialog
- **WHEN** the user taps the shot plan text on IdlePage
- **THEN** the BrewDialog opens with current values populated from Settings (DYE metadata and profile defaults)
- **AND** the targetWeight and targetTemperature are set with a precedence order: overrides first, then profile defaults

#### Scenario: Temperature offset control
- **WHEN** the BrewDialog opens with no recipe active and no temperature override active
- **THEN** the "Temp Delta:" control reads `0°`
- **AND** the profile's actual temperature(s) are shown below it using the adaptive notation: a single value for a one-temperature profile, a spaced mid-dot list for two distinct temperatures (e.g. "Profile: 90 · 88°C"), or first-step…last-step ellipsis for three or more
- **WHEN** the user adjusts the control to `+2°`
- **THEN** every frame's temperature is raised by 2°C for the next shot (the profile structure shown below is unchanged)
- **AND** a "Update Profile" action permanently bakes the `+2°` offset into every frame

#### Scenario: Temperature offset anchored on the active recipe
- **WHEN** the BrewDialog opens with a recipe active whose `tempOverrideC` is 4°C below the profile default
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
- **WHEN** a recipe with `yieldG` = 36 and a temperature 4°C below the profile default is active, the user has dialed a per-brew deviation (e.g. Stop-at 40, Temp Delta +2°), and the user taps "Clear"
- **THEN** Stop-at returns to 36 and the Temp Delta returns to `0°` (the recipe's temperature)
- **AND** the recipe's stored `yieldG` / `tempOverrideC` are unchanged (Clear does not edit the recipe)
