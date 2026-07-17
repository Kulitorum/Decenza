# recipe-activation Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline (`applyLoadedShotMetadata` semantics: profile by title with stored-JSON fallback, bag selected before DYE field writes, queued dose write). QML pill taps, the MCP `recipe_activate` tool, and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly (no bean-identity resolution at activation time).

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, equipment package is selected, dose/yield/temperature apply, grind/rpm apply from the recipe's own owned values (recipe-model), and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via QML, MCP, or the web API
- **THEN** the resulting app and machine state are identical

### Requirement: Optionality ladder is never violated
No brewing, steaming, or navigation flow SHALL require a recipe. With no recipe active, all settings (including steam) SHALL behave exactly as before this change. Activating a recipe that lacks optional rungs SHALL apply what the recipe has: a missing equipment rung leaves the current equipment untouched, while a missing bean rung SHALL **clear the active bag** — the session moves to the "no beans selected" state rather than staying attributed to whatever bag happened to be active before. The system SHALL NOT prompt users to create recipes.

#### Scenario: Recipe-less user
- **WHEN** a user never creates a recipe
- **THEN** every existing flow (profile selection, bag write-through, global steam settings) is byte-for-byte unchanged

#### Scenario: Bean-less recipe activation clears the bag
- **WHEN** a recipe with no linked bean is activated while some bag is active
- **THEN** profile, dose, steam, and the recipe's own grind apply, and the active bag is cleared ("no beans selected")
- **AND** subsequent grind edits and the shot's bean attribution touch no bag at all

#### Scenario: Clearing the bag does not deactivate the bean-less recipe
- **WHEN** a bean-less recipe's activation clears the active bag
- **THEN** the recipe remains the active recipe — the ingredient-swap deactivation watcher treats "no bag" as matching a recipe that has no bag

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

### Requirement: Same-recipe re-activation does not race in-flight edits
Re-activating a recipe that is already the active recipe SHALL NOT overwrite a field whose edit is still being persisted (a write-through to that same recipe row that has not yet been acknowledged). Re-activation of any *other* recipe, or first activation of the currently-active recipe id from a fresh app session, is unaffected and always applies the full bundle from the current stored state.

#### Scenario: Re-tapping the active recipe pill preserves an unsent edit
- **WHEN** the user edits the active recipe's grind, and before that edit is acknowledged by storage the user re-taps the same recipe's pill (triggering re-activation)
- **THEN** the live grind value after re-activation is the user's edit, not a stale pre-edit value

#### Scenario: Re-activation still reflects a settled external edit
- **WHEN** the active recipe was edited from another client (MCP or web) and no local write to that recipe is in flight
- **THEN** re-activating the recipe (or any trigger that re-applies it) reflects the externally-made change

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

### Requirement: Activation applies the recipe's yield anchor after the dose lands

Activation SHALL apply the recipe's yield spec (`yield-anchor`) to the session anchor verbatim — value **and** mode — so an activated recipe opens with its saved yield, and a ratio-moded recipe stays ratio-moded.

When the recipe's mode is `ratio`, the gram target SHALL be derived **after** the recipe's dose has landed, never against the dose in effect before activation. Today `applyActivatedRecipe` writes the yield synchronously while the dose is deferred to a queued write (so that it beats the profile's own deferred `recommendedDose`); an inline `dose × ratio` at the synchronous point would multiply a stale, pre-activation dose. The derivation SHALL therefore happen in the same queued step as the dose, or resolve against the recipe's own `doseG` directly rather than reading it back from settings.

Applying a `ratio` anchor SHALL NOT be suppressed when the derived target happens to equal the active profile's `target_weight`. The "a value matching the profile default is not an override" rule applies to `absolute` yields only — for a ratio it would silently discard the anchor exactly when it coincides with the profile, taking the dose-tracking behaviour with it.

When the recipe's mode is `none`, activation SHALL leave the ladder to fall through to the bag, then the profile — arming no yield anchor of its own.

#### Scenario: A ratio recipe activates ratio-anchored
- **WHEN** a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated
- **THEN** the session anchor is `{2.0, ratio}`, the dose is 18 g, and the target is 36 g
- **AND** a subsequent dose capture of 17.5 g re-derives the target to 35 g

#### Scenario: The ratio resolves against the recipe's dose, not the previous one
- **WHEN** the live dose is 20 g and a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated
- **THEN** the resulting target is 36 g (2.0 × the recipe's 18 g), never 40 g (2.0 × the stale 20 g)

#### Scenario: A derived target equal to the profile default stays anchored
- **WHEN** a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated on a profile whose `target_weight` is 36 g
- **THEN** the session anchor is `{2.0, ratio}` and `hasBrewYieldOverride` is true
- **AND** a subsequent dose change still re-derives the target

#### Scenario: An absolute recipe activates absolute-anchored
- **WHEN** a recipe holding `{36.0, absolute}` is activated
- **THEN** the session anchor is `{36.0, absolute}`
- **AND** a subsequent dose capture of 17.5 g leaves the target at 36 g

#### Scenario: A recipe with no yield falls through the ladder
- **WHEN** a recipe whose yield mode is `none` is activated while the active bag holds `{3.0, ratio}`
- **THEN** the session anchor is the bag's `{3.0, ratio}`

#### Scenario: A recipe with no yield and no bag anchor uses the profile
- **WHEN** a recipe whose yield mode is `none` is activated and the active bag's mode is also `none`
- **THEN** the effective yield is the profile's `target_weight`

