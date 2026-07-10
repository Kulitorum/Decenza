# recipe-activation Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline (`applyLoadedShotMetadata` semantics: profile by title with stored-JSON fallback, bag selected before DYE field writes, queued dose write). QML pill taps, the MCP `recipe_activate` tool, and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly (no bean-identity resolution at activation time).

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, equipment package is selected, dose/yield/temperature apply, grind resolves per inherit-or-pin against the linked bag, and steam settings are written

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
While a recipe is active, changes to dose, yield, temperature override, steam values, milk weight, or the hot-water selection (the chosen water vessel and its values) SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind changes SHALL route to the bag when inherited and to the pin when pinned. Manually changing the profile, the active bag/bean, or the equipment package SHALL deactivate the recipe (event-based, no timers); the recipe itself SHALL be unchanged by deactivation.

#### Scenario: Dose tweak while active
- **WHEN** the user changes dose while a recipe is active
- **THEN** the recipe's stored dose updates

#### Scenario: Hot-water tweak while active
- **WHEN** the user changes the selected water vessel (or its values) while a recipe is active
- **THEN** the recipe's stored hot-water block updates to the new vessel snapshot, and re-selecting the same vessel does not deactivate the recipe

#### Scenario: Inherited grind tweak while active
- **WHEN** the user adjusts grind while a recipe with inherited grind is active
- **THEN** the bag's grind updates (sibling recipes follow) and the recipe stores no pin

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged

### Requirement: Hot-water settings apply on recipe activation
Activating a recipe with a hot-water block SHALL re-select the snapshotted water vessel into the live brew settings and apply its values (temperature, amount by weight or volume, and flow) via the existing hot-water settings path, so the machine is configured to dispense the specified hot water. If the named vessel preset no longer exists, activation SHALL recreate it from the by-value snapshot rather than fail. Unlike the steam heater, hot water needs no multi-minute pre-warm, so activation SHALL NOT introduce any heater hold for the hot-water block. A recipe with a hot-water block but no milk block SHALL NOT force the steam heater on. Activation SHALL remain a single path shared by all surfaces and SHALL apply the hot-water block alongside profile, bean, equipment, grind, dose/yield/temperature, and steam.

#### Scenario: Activation configures hot water from the vessel
- **WHEN** a recipe with a hot-water block is activated from any surface
- **THEN** the snapshotted vessel becomes the selected water vessel and its temperature, amount, and flow are applied to the live hot-water settings

#### Scenario: Milk-less hot-water recipe does not warm the steam heater
- **WHEN** a recipe with a hot-water block and no milk block is activated and the user has `keepSteamHeaterOn` disabled
- **THEN** the steam heater is not forced on by the hot-water block

#### Scenario: Missing vessel preset falls back to the snapshot
- **WHEN** a recipe's hot-water block references a water-vessel preset that has since been deleted
- **THEN** activation applies the by-value snapshot stored on the recipe rather than failing

### Requirement: Profile-less recipes activate without the profile pipeline
When an activated recipe has no profile, the activation path SHALL skip the profile-load, dose-write, and yield/temperature-override stages entirely and SHALL still apply the linked bag, equipment selection, and the hot-water block (vessel re-select by name with snapshot recreation, then hot-water settings apply). No steam-heater hold SHALL be taken (the existing hot-water rule). The currently loaded espresso profile SHALL be left untouched. `recipeActivated` SHALL fire with the same terminal semantics as profile-carrying recipes, and `activeRecipeId` SHALL be set last.

#### Scenario: Hot-water tea activation
- **WHEN** a profile-less tea recipe is activated from any surface
- **THEN** the tea bag becomes active, the snapshotted vessel is selected and its values applied, the loaded profile is unchanged, and no steam heating starts

#### Scenario: Zero shots forever
- **WHEN** the user brews hot water repeatedly with a profile-less recipe active
- **THEN** the recipe accumulates no shots and remains hard-deletable

### Requirement: Stale recipes activate with the finished bag's data
Activating a recipe whose linked bag is finished SHALL apply the full bundle: profile, dose/yield/temperature, equipment, steam, and hot water apply normally, and grind resolves per inherit-or-pin against the finished bag (its last dial is a better starting point than nothing). The finished bag SHALL NOT be returned to inventory by activation. Activation SHALL succeed with no error or dialog; the stale indication remains a display concern of the listing surfaces.

#### Scenario: Pulling a shot from a stale recipe
- **WHEN** the user activates a stale recipe and starts espresso
- **THEN** the shot runs with the recipe's profile, numbers, and the finished bag's grind — identical to activation before the bag was finished

