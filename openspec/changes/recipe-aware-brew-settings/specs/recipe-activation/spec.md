## MODIFIED Requirements

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, changes to dose, steam values, milk weight, or the hot-water selection (the chosen water vessel and its values) SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind/RPM changes SHALL write through to the active bag and stamp the recipe's own `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`: grind lives on the recipe, the bag always mirrors the last dial, and a grind-less `tea*` recipe never adopts a grind).

Yield and temperature are per-brew **overrides**, not tweaks: a Brew Settings change to yield (Stop-at) or temperature (Temp Delta) SHALL apply only as a `Settings.brew` override for the next brew and SHALL NOT write through to the active recipe. The recipe's `yieldG` / `tempOverrideC` SHALL change only via an explicit "Update Recipe" action. Accordingly, the `MainController` auto-stamp watchers on `SettingsBrew::brewOverridesChanged` (→ `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ `tempOverrideC`) SHALL be removed.

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
- **AND** the active recipe's `tempOverrideC` is unchanged

#### Scenario: Hot-water tweak while active
- **WHEN** the user changes the selected water vessel (or its values) while a recipe is active
- **THEN** the recipe's stored hot-water block updates to the new vessel snapshot, and re-selecting the same vessel does not deactivate the recipe

#### Scenario: Grind tweak while active
- **WHEN** the user adjusts grind (or RPM) while a non-tea recipe is active
- **THEN** the recipe's own `grindPinned`/`rpmPinned` updates and the setter mirrors the value onto the linked bag (no inherit/pin routing)

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged
