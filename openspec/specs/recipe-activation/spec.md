# recipe-activation Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline (`applyLoadedShotMetadata` semantics: profile by title with stored-JSON fallback, bag selected before DYE field writes, queued dose write). QML pill taps, the MCP `recipe_activate` tool, and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`.

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bean's open bag becomes active, equipment package is selected, dose/yield/temperature apply, grind resolves per inherit-or-pin, and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via QML, MCP, or the web API
- **THEN** the resulting app and machine state are identical

### Requirement: Optionality ladder is never violated
No brewing, steaming, or navigation flow SHALL require a recipe. With no recipe active, all settings (including steam) SHALL behave exactly as before this change. Activating a recipe that lacks optional rungs (no bean, no equipment) SHALL apply what the recipe has and leave the missing rungs untouched. The system SHALL NOT prompt users to create recipes.

#### Scenario: Recipe-less user
- **WHEN** a user never creates a recipe
- **THEN** every existing flow (profile selection, bag write-through, global steam settings) is byte-for-byte unchanged

#### Scenario: Bean-less recipe activation
- **WHEN** a recipe with no linked bean is activated
- **THEN** profile, dose, steam, and recipe-local grind apply, and the active bag is not changed

### Requirement: Steam settings write on recipe switch with a held heater state
Activating a recipe SHALL write its steam block into the live brew settings (propagating to the DE1 as today) at activation time, not at shot start. Because the steam heater takes 5–9 minutes to reach temperature, an active milk recipe (`hasMilk: true`) SHALL HOLD the heater on for as long as it is active and the machine is awake: every machine-settings send SHALL treat an active milk recipe like `keepSteamHeaterOn`, so re-sends (wake, reconnect, settings edits) keep the heater warm. Deactivating (or switching to a milk-less recipe) SHALL return the heater to the user's baseline. When `keepSteamHeaterOn` is enabled by the user, a milk-less recipe SHALL NOT override it to off. No new user-facing steam-mode setting SHALL be added.

#### Scenario: Milk recipe selected
- **WHEN** a recipe with `hasMilk: true` is activated
- **THEN** steam temperature/flow/timeout and pitcher/milk weight apply immediately and the heater begins warming

#### Scenario: Heater hold survives settings re-sends
- **WHEN** a milk recipe is active with `keepSteamHeaterOn` disabled and machine settings are re-sent (wake, reconnect, an unrelated settings edit)
- **THEN** the steam heater target stays on

#### Scenario: Leaving the milk recipe releases the hold
- **WHEN** the active milk recipe is deactivated (or a milk-less recipe is activated) and the user has `keepSteamHeaterOn` disabled
- **THEN** the heater returns to off

#### Scenario: Milk-less recipe with keep-heater-on user
- **WHEN** a recipe with `hasMilk: false` is activated and the user has `keepSteamHeaterOn` enabled
- **THEN** the heater stays warm

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, changes to dose, yield, temperature override, steam values, or milk weight SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind changes SHALL route to the bag when inherited and to the pin when pinned. Manually changing the profile, the active bag/bean, or the equipment package SHALL deactivate the recipe (event-based, no timers); the recipe itself SHALL be unchanged by deactivation.

#### Scenario: Dose tweak while active
- **WHEN** the user changes dose while a recipe is active
- **THEN** the recipe's stored dose updates

#### Scenario: Inherited grind tweak while active
- **WHEN** the user adjusts grind while a recipe with inherited grind is active
- **THEN** the bag's grind updates (sibling recipes follow) and the recipe stores no pin

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged

