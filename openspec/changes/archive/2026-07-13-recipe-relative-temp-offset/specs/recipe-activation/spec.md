# recipe-activation Delta

## ADDED Requirements

### Requirement: Activation derives the brew temperature from the offset
When a profile-carrying recipe with a non-zero `tempOffsetC` is activated, the activation path SHALL compute the brew temperature as `loaded profile's espresso_temperature + offset` and apply it as the per-brew temperature override (uploading the profile as today). A recipe with offset 0 SHALL arm no temperature override — this replaces the old "recipe value coincidentally equals the profile default is not an override" guard (Bug A), which becomes unnecessary because a delta of zero is unambiguous. The recipe-baseline accessors used by Brew Settings and the Shot Plan (`activeBaselineTemperatureC`) SHALL return the same offset-derived temperature.

#### Scenario: Offset applies against the current profile temperature
- **WHEN** a recipe with offset −3 on a profile whose espresso_temperature is 90 is activated
- **THEN** the brew temperature override becomes 87 and the Shot Plan / Brew Settings baseline reads 87

#### Scenario: Recipe follows a profile temperature edit
- **WHEN** that profile's temperature is later saved as 88 and the recipe is activated again
- **THEN** the brew temperature override becomes 85 — the recipe kept its designed −3° relationship, and its editor still shows −3°

#### Scenario: Zero offset arms no override
- **WHEN** a recipe with offset 0 is activated
- **THEN** no temperature override is armed and the machine brews at the profile's own temperature

## MODIFIED Requirements

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, changes to dose, steam values, milk weight, or the hot-water selection (the chosen water vessel and its values) SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind/RPM changes SHALL write through to the active bag and stamp the recipe's own `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`: grind lives on the recipe, the bag always mirrors the last dial, and a grind-less `tea*` recipe never adopts a grind).

Yield and temperature are per-brew **overrides**, not tweaks: a Brew Settings change to yield (Stop-at) or temperature (Temp Delta) SHALL apply only as a `Settings.brew` override for the next brew and SHALL NOT write through to the active recipe. The recipe's `yieldG` / `tempOffsetC` SHALL change only via an explicit "Update Recipe" action; when Update Recipe persists a temperature, it SHALL store the delta between the dialed temperature and the profile's espresso_temperature (the offset), never the absolute value. Accordingly, the `MainController` auto-stamp watchers on `SettingsBrew::brewOverridesChanged` (→ `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ `tempOffsetC`) SHALL be removed.

Manually changing the profile, the active bag/bean, or the equipment package SHALL deactivate the recipe (event-based, no timers); the recipe itself SHALL be unchanged by deactivation.

#### Scenario: Dose tweak while active
- **WHEN** the user changes dose while a recipe is active
- **THEN** the recipe's stored dose updates

#### Scenario: Yield override while active does not change the recipe
- **WHEN** the user changes yield (Stop-at) while a recipe is active and commits the brew
- **THEN** the change applies as a `Settings.brew` override for the brew
- **AND** the active recipe's `yieldG` is unchanged

#### Scenario: Temperature override while active does not change the recipe
- **WHEN** the user changes the temperature (Temp Delta) while a recipe is active and commits the brew
- **THEN** the change applies as a `Settings.brew` override for the brew
- **AND** the active recipe's `tempOffsetC` is unchanged

#### Scenario: Update Recipe stores the temperature as an offset
- **WHEN** the user dials the brew temperature to 87 on a 90° profile while a recipe is active and presses Update Recipe on the temperature
- **THEN** the recipe stores `tempOffsetC` = −3

#### Scenario: Hot-water tweak while active
- **WHEN** the user changes the selected water vessel (or its values) while a recipe is active
- **THEN** the recipe's stored hot-water block updates to the new vessel snapshot, and re-selecting the same vessel does not deactivate the recipe

#### Scenario: Grind tweak while active
- **WHEN** the user adjusts grind (or RPM) while a non-tea recipe is active
- **THEN** the recipe's own `grindPinned`/`rpmPinned` updates and the setter mirrors the value onto the linked bag (no inherit/pin routing)

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged
