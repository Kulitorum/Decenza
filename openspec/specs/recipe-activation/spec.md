# recipe-activation Specification

## Purpose
Governs how a recipe is activated from every surface (QML, MCP, ShotServer) and what activation configures on the machine, including the profile, bag, equipment, dose, yield, temperature, steam and hot water. Also covers when a user or profile change deactivates the active recipe, and how recipes whose profile is missing are shown and handled.

## Requirements

### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline. QML pill taps and the MCP `recipe_activate` tool SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly, with no bean-identity resolution at activation time.

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, the recipe equipment package is selected when present, dose/yield/temperature apply, grind/rpm apply from the recipe's own owned values (recipe-model), and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via native QML, browser QML, or MCP
- **THEN** the resulting app and machine state are identical

#### Scenario: Shot-load ordering
- **WHEN** a recipe is activated
- **THEN** the profile is loaded by title with a stored-JSON fallback, the bag is selected before DYE field writes, and the dose is written through the queued dose write

### Requirement: Optionality ladder is never violated
No brewing, steaming, or navigation flow SHALL require a recipe. With no recipe active, recipe-free brewing and equipment selection SHALL remain available. A missing equipment rung SHALL leave the current equipment untouched. A missing bean rung SHALL clear the active bag. The system SHALL NOT prompt users to create recipes.

#### Scenario: Recipe-less user
- **WHEN** a user never creates a recipe
- **THEN** profile selection, manual equipment selection and global steam settings remain available

#### Scenario: Bean-less recipe activation clears the bag
- **WHEN** a recipe with no linked bean is activated while some bag is active
- **THEN** profile, dose, steam, and the recipe's own grind apply, and the active bag is cleared ("no beans selected")
- **AND** subsequent grind edits and the shot's bean attribution touch no bag at all

#### Scenario: Clearing the bag does not deactivate the bean-less recipe
- **WHEN** a bean-less recipe's activation clears the active bag
- **THEN** the recipe remains the active recipe — the ingredient-swap deactivation watcher treats "no bag" as matching a recipe that has no bag

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, a dose change SHALL write through to the active recipe, with no dirty state. Grind and RPM changes SHALL stamp the recipe's own `grindPinned`/`rpmPinned`. Dose and grind/RPM are the only live dial values that reach the recipe; there is no other write-through.

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
- **WHEN** the user selects a different water vessel while a recipe with a hot-water block is active
- **THEN** the recipe is deactivated and its pill deselects
- **AND** the new vessel's amount, temperature, and flow apply to the live settings
- **AND** the recipe's stored hot-water block is unchanged

#### Scenario: Re-selecting the recipe's own vessel does not deactivate
- **WHEN** the user taps the water vessel the active recipe's block already names
- **THEN** the recipe stays active

#### Scenario: A recipe with no hot-water block is not deactivated by a vessel change
- **WHEN** the user selects a different water vessel while an espresso recipe is active
- **THEN** the recipe stays active, because it owns no vessel choice

#### Scenario: Steam pitcher change while active deactivates
- **WHEN** the user selects a different steam pitcher while a recipe whose steam block names one is active
- **THEN** the recipe is deactivated and the recipe's stored steam block is unchanged
- **AND** the selected pitcher is the one the user picked, not the parked standing pitcher

#### Scenario: Editing a preset does not deactivate
- **WHEN** the user edits the amount of the water-vessel preset the active recipe's block names, or edits a steam pitcher preset
- **THEN** the recipe stays active and its stored block keeps its by-value snapshot

#### Scenario: The measured milk weight does not deactivate
- **WHEN** a steam session ends and the measured milk weight is captured while a milk recipe is active
- **THEN** the recipe stays active, so the shot that follows is still attributed to it

#### Scenario: Re-activating the recipe restores its vessel and pitcher
- **WHEN** the user changes the water vessel (or steam pitcher) while a recipe is active, then activates that same recipe again
- **THEN** the recipe's stored vessel and pitcher are re-selected and their values re-applied to the live settings

#### Scenario: The wizard is the way to keep a block change
- **WHEN** the user wants a recipe to pour a different amount of hot water from now on
- **THEN** the change is made by editing the recipe (the wizard, MCP `recipe_update`, or the web recipe form), never by a live selection

#### Scenario: Grind tweak while active
- **WHEN** the user adjusts grind (or RPM) while a non-tea recipe is active
- **THEN** the recipe's own `grindPinned`/`rpmPinned` updates without writing to the bag

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged

#### Scenario: Deleting the recipe's profile deactivates
- **WHEN** the user deletes the profile that the active recipe names
- **THEN** the active recipe clears and its pill deselects
- **AND** the recipe's stored fields, including the now-dangling profile title, are unchanged

#### Scenario: Deleting an unrelated profile does not deactivate
- **WHEN** the user deletes a profile that the active recipe does not name
- **THEN** the recipe stays active

#### Scenario: A restored recipe meeting a different profile deactivates
- **WHEN** the app restarts with a recipe selected and the profile that loads is not the one the recipe names
- **THEN** the recipe is deactivated once its row arrives, before any shot can be recorded against it

#### Scenario: A restored recipe meeting its own profile stays active
- **WHEN** the app restarts with a recipe selected and the profile that loads is the one the recipe names
- **THEN** the recipe stays active and its pill stays selected

#### Scenario: An edit that re-points the recipe's profile deactivates
- **WHEN** the active recipe is edited from another surface so that it names a different profile than the loaded one
- **THEN** the recipe is deactivated

#### Scenario: Editing the loaded profile in place does not deactivate
- **WHEN** the user edits the loaded profile and saves it under the same title while a recipe naming it is active
- **THEN** the recipe stays active

#### Scenario: A profile-less recipe survives a profile change
- **WHEN** the user selects a different profile while a profile-less recipe (tea, hot water) is active
- **THEN** the recipe stays active, because it owns no profile choice

#### Scenario: A shot is never recorded against a recipe whose profile it did not run
- **WHEN** a shot is recorded while a recipe is active
- **THEN** the loaded profile is the one that recipe names, or the recipe was deactivated before the shot and the shot carries no recipe

### Requirement: Yield and temperature are per-brew overrides
Yield and temperature are per-brew overrides. A Brew Settings change to yield or temperature SHALL apply only as a `Settings.brew` override for the next brew, never writing through to the recipe. `yieldG` and `tempOffsetC` SHALL change only through "Update Recipe", which stores temperature as an offset from the profile's espresso_temperature. The auto-stamp watchers on `brewOverridesChanged` and `temperatureOverrideChanged` SHALL be removed.

#### Scenario: Update Recipe stores the temperature as an offset
- **WHEN** the user dials the brew temperature to 87 on a 90° profile while a recipe is active and presses Update Recipe on the temperature
- **THEN** the recipe stores `tempOffsetC` = −3

### Requirement: Steam pitcher and water vessel are ingredients
The steam pitcher and the water vessel are ingredients, not tweaks. Selecting a different pitcher or vessel while a recipe is active SHALL deactivate the recipe, and the change SHALL apply to the live settings while the recipe's stored block is left untouched. The auto-stamp watchers on pitcher and vessel selection and presets SHALL be removed and replaced by two deactivation watchers.

#### Scenario: Steam pitcher change while active deactivates
- **WHEN** the user selects a different steam pitcher while a recipe whose steam block names one is active
- **THEN** the recipe is deactivated and the recipe's stored steam block is unchanged
- **AND** the selected pitcher is the one the user picked, not the parked standing pitcher

### Requirement: Deactivation watchers gate on ownership
Each deactivation watcher SHALL gate on ownership first. A recipe whose steam block names no pitcher SHALL NOT be deactivated by a pitcher change, and a recipe with no active hot-water block SHALL NOT be deactivated by a vessel change. Re-selecting the pitcher or vessel the recipe names SHALL NOT deactivate it. The ownership and divergence checks SHALL each have one definition, beside `Recipe::profileDiverged`, shared by every site.

#### Scenario: Re-selecting the recipe's own vessel does not deactivate
- **WHEN** the user taps the water vessel the active recipe's block already names
- **THEN** the recipe stays active

#### Scenario: A recipe with no hot-water block is not deactivated by a vessel change
- **WHEN** the user selects a different water vessel while an espresso recipe is active
- **THEN** the recipe stays active, because it owns no vessel choice

### Requirement: Presets and milk weight do not deactivate
Editing, adding, removing or reordering a pitcher or vessel preset SHALL NOT deactivate the active recipe, because a recipe's block is a by-value snapshot. The steamed-milk weight SHALL NOT deactivate either. It is captured at the end of a steam session, and deactivating on it would cost the next shot its recipe attribution.

#### Scenario: The measured milk weight does not deactivate
- **WHEN** a steam session ends and the measured milk weight is captured while a milk recipe is active
- **THEN** the recipe stays active, so the shot that follows is still attributed to it

### Requirement: Deactivation preserves the user's pitcher pick
Deactivation SHALL NOT undo the selection that triggered it. A deactivation driven by the user's own pitcher pick SHALL first make that pick the standing selection, so that the unwind does not re-select the parked pitcher.

#### Scenario: Re-activating the recipe restores its vessel and pitcher
- **WHEN** the user changes the water vessel (or steam pitcher) while a recipe is active, then activates that same recipe again
- **THEN** the recipe's stored vessel and pitcher are re-selected and their values re-applied to the live settings

### Requirement: Recipe blocks change only by explicit edit
A recipe's `steamJson` and `hotWaterJson` SHALL change only through an explicit recipe edit: the wizard, `recipe_update` over MCP, or the web recipe form. Re-activating the recipe SHALL restore its pitcher and vessel into the live settings.

#### Scenario: The wizard is the way to keep a block change
- **WHEN** the user wants a recipe to pour a different amount of hot water from now on
- **THEN** the change is made by editing the recipe (the wizard, MCP `recipe_update`, or the web recipe form), never by a live selection

### Requirement: Bag or equipment change deactivates
Changing the active bag or bean, or the equipment package, SHALL deactivate the recipe. This SHALL be event-based, with no timers. Deactivation SHALL leave the recipe itself unchanged. The explicitly accepted shot-review correction described by `shot-review-carry-forward` is an exception: it relinks the active recipe and keeps it active without importing other bag settings.

#### Scenario: Equipment change deactivates the recipe
- **WHEN** the user changes the active bag or the equipment package while a recipe is active outside the confirmed shot-review correction
- **THEN** the recipe is deactivated and its stored fields are unchanged

#### Scenario: Accepted shot-review bean correction
- **WHEN** the user accepts a shot-review offer that includes updating the active recipe's bag link
- **THEN** that recipe remains active with the corrected bag
- **AND** its other fields and the current next-shot brew settings are preserved except for separately listed accepted corrections

### Requirement: Profile change deactivates by any route
The active recipe SHALL be deactivated whenever the profile it names ceases to be the loaded profile, by any route: the user selecting a different profile, the named profile being deleted, or a restored selection meeting a different profile. This SHALL be a single shared predicate consulted at every such site, not a comparison written independently at each one.

#### Scenario: Profile swap deactivates
- **WHEN** the user manually selects a different profile while a recipe is active
- **THEN** the active recipe clears, its pill deselects, and the recipe's stored fields are unchanged

### Requirement: Profile-less and in-place profile edits do not deactivate
A recipe that names no profile SHALL NOT be deactivated by any profile change, because it owns no profile choice. Editing the loaded profile in place SHALL NOT deactivate the recipe, because the recipe names a title and an edit that keeps the title has not changed what it points at.

#### Scenario: A profile-less recipe survives a profile change
- **WHEN** the user selects a different profile while a profile-less recipe (tea, hot water) is active
- **THEN** the recipe stays active, because it owns no profile choice

#### Scenario: Editing the loaded profile in place does not deactivate
- **WHEN** the user edits the loaded profile and saves it under the same title while a recipe naming it is active
- **THEN** the recipe stays active

### Requirement: Hot-water settings apply on recipe activation
Activating a recipe with hot water SHALL select its snapshotted vessel and apply temperature, flow and its fixed or yield-relative amount through the existing hot-water path. Ratio water SHALL resolve after the recipe's espresso target is established. A missing preset SHALL be recreated as a fixed preset using the snapshot's resolved amount. Hot water SHALL take no steam-heater hold or force the heater on for a milk-less recipe.

#### Scenario: Activation configures hot water from the vessel
- **WHEN** a recipe with a hot-water block is activated from any surface
- **THEN** the snapshotted vessel becomes selected and its temperature, resolved amount and flow apply to the live hot-water settings

#### Scenario: Milk-less hot-water recipe does not warm the steam heater
- **WHEN** a recipe with a hot-water block and no milk block is activated and the user has `keepSteamHeaterOn` disabled
- **THEN** the steam heater is not forced on by the hot-water block

#### Scenario: Missing vessel preset falls back to the snapshot
- **WHEN** a recipe's hot-water block references a water-vessel preset that has since been deleted
- **THEN** activation applies the by-value snapshot stored on the recipe rather than failing
- **AND** a ratio snapshot recreates a fixed preset at its currently resolved grams while retaining the ratio on the recipe

#### Scenario: Hot water applied with the rest of the bundle
- **WHEN** a recipe with a hot-water block is activated
- **THEN** the hot-water block applies alongside profile, bean, equipment, grind, dose/yield/temperature and steam through the same single activation path

#### Scenario: Resolve after the new recipe's yield lands
- **WHEN** a recipe with an 18 g dose, 1:2 espresso yield and 2.5× added water replaces a recipe targeting 50 g espresso
- **THEN** its espresso target is 36 g and added water is 90 g
- **AND** the old target never produces a retained 125 g water target

### Requirement: Profile-less recipes activate without the profile pipeline
When an activated recipe has no profile, activation SHALL skip the profile-load, dose-write and yield/temperature-override stages, and SHALL still apply the linked bag, equipment and hot-water block. No steam-heater hold SHALL be taken, and the loaded espresso profile SHALL be left untouched. `recipeActivated` SHALL fire with the same terminal semantics, and `activeRecipeId` SHALL be set last.

#### Scenario: Hot-water tea activation
- **WHEN** a profile-less tea recipe is activated from any surface
- **THEN** the tea bag becomes active, the snapshotted vessel is selected and its values applied, the loaded profile is unchanged, and no steam heating starts

#### Scenario: Zero shots forever
- **WHEN** the user brews hot water repeatedly with a profile-less recipe active
- **THEN** the recipe accumulates no shots and remains hard-deletable

### Requirement: Stale recipes activate with the finished bag's data
Activating a recipe whose linked bag is finished SHALL apply the full bundle: profile, dose/yield/temperature, equipment, steam, and hot water apply normally, and the recipe's own grind applies. The finished bag SHALL NOT be returned to inventory by activation. Activation SHALL succeed with no error or dialog; the stale indication remains a display concern of the listing surfaces.

#### Scenario: Pulling a shot from a stale recipe
- **WHEN** the user activates a stale recipe and starts espresso
- **THEN** the shot runs with the recipe's profile, numbers, and the recipe's own grind — identical to activation before the bag was finished

### Requirement: Same-recipe re-activation does not race in-flight edits
Re-activating a recipe that is already the active recipe SHALL NOT overwrite a field whose edit is still being persisted (a write-through to that same recipe row that has not yet been acknowledged). Re-activation of any *other* recipe, or first activation of the currently-active recipe id from a fresh app session, is unaffected and always applies the full bundle from the current stored state.

#### Scenario: Re-tapping the active recipe pill preserves an unsent edit
- **WHEN** the user edits the active recipe's grind, and before that edit is acknowledged by storage the user re-taps the same recipe's pill (triggering re-activation)
- **THEN** the live grind value after re-activation is the user's edit, not a stale pre-edit value

#### Scenario: Re-activation still reflects a settled external edit
- **WHEN** the active recipe was edited from another client (MCP or web) and no local write to that recipe is in flight
- **THEN** re-activating the recipe (or any trigger that re-applies it) reflects the externally-made change

### Requirement: Activation derives the brew temperature from the offset
When a profile-carrying recipe with a non-zero `tempOffsetC` is activated, the brew temperature SHALL be the loaded profile's espresso_temperature plus the offset, applied as the per-brew temperature override. A recipe with offset 0 SHALL arm no temperature override. The recipe-baseline accessor `activeBaselineTemperatureC`, used by Brew Settings and the Shot Plan, SHALL return the same offset-derived temperature.

#### Scenario: Offset applies against the current profile temperature
- **WHEN** a recipe with offset −3 on a profile whose espresso_temperature is 90 is activated
- **THEN** the brew temperature override becomes 87 and the Shot Plan / Brew Settings baseline reads 87

#### Scenario: Recipe follows a profile temperature edit
- **WHEN** that profile's temperature is later saved as 88 and the recipe is activated again
- **THEN** the brew temperature override becomes 85 — the recipe kept its designed −3° relationship, and its editor still shows −3°

#### Scenario: Zero offset arms no override
- **WHEN** a recipe with offset 0 is activated
- **THEN** no temperature override is armed and the machine brews at the profile's own temperature

#### Scenario: Override is uploaded with the profile
- **WHEN** a profile-carrying recipe with a non-zero offset is activated
- **THEN** the profile is uploaded to the machine as today, carrying the derived temperature override

### Requirement: Activation applies the recipe's yield anchor after the dose lands
Activation SHALL apply the recipe's yield spec to the session anchor verbatim, value and mode, so a ratio-moded recipe stays ratio-moded. For a `ratio` recipe the gram target SHALL be derived after the recipe's dose has landed, in the same queued step as the dose or against the recipe's own `doseG`, never the pre-activation dose. A `none` mode SHALL arm no anchor, so the ladder falls through to the bag, then the profile.

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

### Requirement: A ratio anchor is never suppressed as a profile default
Applying a `ratio` anchor SHALL NOT be suppressed when the derived target equals the active profile's `target_weight`. The rule that a value matching the profile default is not an override SHALL apply to `absolute` yields only.

#### Scenario: A ratio anchor equal to the profile default is kept
- **WHEN** a recipe holding `{2.0, ratio}` is activated on a profile whose `target_weight` equals the derived target
- **THEN** the session anchor stays `{2.0, ratio}` and `hasBrewYieldOverride` is true

### Requirement: A recipe whose profile is not installed shows that it is
A recipe that names a profile which is not installed SHALL be visibly marked as missing its profile wherever recipes are listed. The marking SHALL be derived at display time from the loaded profile catalog and SHALL NOT be stored on the recipe. No pass over the recipe library SHALL scan, mark, migrate or repair stored recipes. The marking SHALL follow the catalog and clear on its own when the profile is available again.

#### Scenario: A recipe naming a deleted profile is marked
- **WHEN** the user views the recipe list after deleting a profile that a recipe names
- **THEN** that recipe is shown as missing its profile

#### Scenario: Re-importing the profile clears the marking
- **WHEN** the missing profile is imported again
- **THEN** the recipe is no longer marked, with no repair step

#### Scenario: A profile-less recipe is not marked
- **WHEN** the recipe list shows a profile-less tea or hot-water recipe
- **THEN** it is not marked as missing a profile

#### Scenario: A mistyped profile title is marked
- **WHEN** a recipe is created through MCP or the web with a `profileTitle` that matches no installed profile
- **THEN** it is marked in the list exactly as a deleted-profile recipe is

#### Scenario: Marking covers every route into the state
- **WHEN** a recipe's profile is missing because of a restored database, a device transfer arriving without its profiles, or a `profileTitle` that never resolved
- **THEN** the recipe is marked as missing its profile, with no repair step

### Requirement: A profile-less recipe is never marked
A recipe that names no profile SHALL NOT be marked as missing its profile. It is a valid profile-less drink, not a broken one.

#### Scenario: A profile-less recipe is not marked
- **WHEN** the recipe list shows a profile-less tea or hot-water recipe
- **THEN** it is not marked as missing a profile

### Requirement: A failed activation tells the user why
When activation fails, the user SHALL be shown that it failed and what is wrong, rather than only a selection that reverts. When the cause is a profile that is neither installed nor carried as stored JSON, the message SHALL name the missing profile title, because that is the value the user must change to fix it.

#### Scenario: Activating a recipe whose profile is gone
- **WHEN** the user activates a recipe whose profile is not installed and which carries no stored profile JSON
- **THEN** activation fails, the selection reverts, and the user is shown a message naming the missing profile

#### Scenario: The machine is not left half-configured
- **WHEN** activation fails for a missing profile
- **THEN** the previously loaded profile and all brew settings are unchanged

### Requirement: Deleting a profile warns when recipes name it
Deleting a profile that one or more recipes name SHALL warn the user with how many recipes use it, and SHALL proceed on confirmation. Deletion SHALL NOT be refused. The affected recipes SHALL NOT be modified: no profile snapshot is written into them and their stored `profileTitle` is kept. No repair action SHALL be offered in the delete flow.

#### Scenario: Deleting a profile that recipes use
- **WHEN** the user deletes a profile that three recipes name
- **THEN** the user is warned that three recipes use it and can confirm or cancel

#### Scenario: Confirming the delete
- **WHEN** the user confirms
- **THEN** the profile is deleted, the recipes are unchanged, and they show as missing their profile

#### Scenario: Deleting an unused profile
- **WHEN** the user deletes a profile that no recipe names
- **THEN** no warning is shown

#### Scenario: Repairing a broken recipe
- **WHEN** the user opens a recipe marked as missing its profile in the recipe editor and selects an installed profile
- **THEN** the recipe activates normally afterwards

#### Scenario: Recipes are left missing their profile
- **WHEN** the user confirms the delete of a profile that recipes name
- **THEN** the profile is deleted, the recipes are unchanged, and they show as missing their profile

### Requirement: Steam settings write on recipe switch, heater follows the user's settings
Activating a recipe SHALL write its steam block into the live brew settings at activation time, not at shot start, and SHALL apply the selected pitcher's stored duration, flow and temperature. The recipe's pitcher SHALL be an override of the standing pitcher, not a write to it, and deactivating SHALL restore the standing pitcher unmodified. Activation SHALL NOT grant the steam heater permission to be warm.

#### Scenario: Milk recipe selected
- **WHEN** a recipe with a milk pitcher is activated
- **THEN** its steam temperature, flow and timeout apply immediately from that pitcher, and the heater state follows the user's settings — it is not warmed by the act of selecting

#### Scenario: Milk recipe's shot warms the heater
- **WHEN** Keep warm when idle is off, Let the recipe decide is on, a milk recipe is active, and its shot starts
- **THEN** the steam heater begins warming to that recipe's pitcher temperature

#### Scenario: Warmth from a shot is released at Idle
- **WHEN** the shot and any steaming finish and the machine returns to Idle, with Keep warm when idle off
- **THEN** the steam heater is turned off

#### Scenario: A recipe without a pitcher turns the heater off
- **WHEN** Let the recipe decide is on and a recipe carrying no pitcher is activated
- **THEN** the steam heater is turned off immediately

#### Scenario: Deactivating restores the standing pitcher
- **WHEN** a recipe whose pitcher differs from the standing pitcher is deactivated
- **THEN** the standing pitcher is in effect again, unmodified by the activation

#### Scenario: The recipe is ignored when the user says so
- **WHEN** Let the recipe decide is off and the active recipe names no pitcher
- **THEN** activating it leaves the effective pitcher and the heater state unchanged

#### Scenario: An explicit pitcher choice is a selection, not an inference
- **WHEN** a recipe names a pitcher — a real one, or the built-in "Heater off"
- **THEN** activating it applies that pitcher whatever the two settings say, because the user chose it when they built the recipe
- **AND** a recipe carrying "Heater off" leaves the heater cold whatever the two settings say
- **AND** starting steam on it behaves exactly as it does with "Heater off" standing: the last real pitcher's duration, flow and temperature, with a message saying so

#### Scenario: Milk-less recipe with keep-warm user
- **WHEN** a recipe with no milk is activated, the user has Keep warm when idle on and Let the recipe decide off
- **THEN** the heater stays warm

#### Scenario: Heater state survives a profile upload
- **WHEN** a recipe activation sets the heater state and the profile upload it triggered completes afterwards
- **THEN** the heater state is unchanged by that upload

### Requirement: Heater warms on a recipe shot only by permission
A recipe that uses steam SHALL warm the steam heater when its shot starts, and only when **Let the recipe decide** is on. That permission SHALL be revoked when the machine returns to Idle. A recipe carrying the "Heater off" marker, or carrying no pitcher, SHALL veto the heater immediately on activation when **Let the recipe decide** is on. `hasMilk` SHALL NOT cause the heater to be warmed and SHALL NOT override the user's settings.

#### Scenario: A recipe without a pitcher turns the heater off
- **WHEN** Let the recipe decide is on and a recipe carrying no pitcher is activated
- **THEN** the steam heater is turned off immediately

### Requirement: Ratio water follows the effective espresso target
While a ratio-water recipe is active, its added-water target SHALL update when its effective espresso target changes, including per-brew yield overrides and dose changes that move a ratio-based yield. Activation, restoration or editing the ratio SHALL initialize or refresh the result. The saved ratio and shared preset's fixed amount SHALL NOT be rewritten by this calculation.

#### Scenario: Change target yield
- **WHEN** an active recipe uses 2.5× added water and its espresso target changes from 40 g to 42 g
- **THEN** the added-water target changes from 100 g to 105 g
- **AND** the recipe's ratio remains 2.5

#### Scenario: Dose changes a ratio-based espresso target
- **WHEN** an active recipe uses 1:2 espresso yield and 2.5× added water and its dose changes from 18 g to 20 g
- **THEN** the espresso target changes from 36 g to 40 g and added water from 90 g to 100 g

#### Scenario: Dose does not change an absolute espresso target
- **WHEN** the same dose change occurs with an absolute espresso target of 40 g
- **THEN** added water stays at 100 g

#### Scenario: Override does not persist a recipe edit
- **WHEN** the user temporarily changes the active recipe's espresso target to 44 g
- **THEN** its 2.5× added water becomes 110 g for that setup
- **AND** neither its stored espresso yield nor its stored water ratio is changed by the override

#### Scenario: Edit only the water ratio
- **WHEN** the active recipe's ratio is explicitly edited from 2.5 to 3 with the espresso target staying at 40 g
- **THEN** its added-water target becomes 120 g without requiring reactivation

#### Scenario: Edit fixed water on the active recipe
- **WHEN** an active recipe's fixed amount is edited from 100 g to 150 g while no drink is in progress
- **THEN** the same recipe-water application path applies 150 g without reactivation

#### Scenario: Temporary espresso bumps are scoped to one drink
- **WHEN** an Americano targets 40 g, the user deliberately bumps to 42 g and then back to 40 g before water
- **THEN** its 2.5× water target follows 100 g, 105 g and 100 g
- **AND** the adjustment ends with that drink on completion or abort and never leaks into a replacement recipe's setup

#### Scenario: Restore after restart
- **WHEN** a ratio-water recipe is restored as active after restart
- **THEN** its water target is initialized from the restored effective espresso target rather than a stale cached water amount

### Requirement: Water resolution is based on target changes rather than drinks
Ratio-water calculation SHALL use the current intended espresso target, never measured shot output, tare readings or drip settling. Repeating a drink with unchanged recipe water settings and espresso target SHALL reuse its resolved water target. An in-progress water pour SHALL NOT be retargeted automatically; a target change during that pour SHALL apply after the drink completes.

#### Scenario: Actual shot differs from target
- **WHEN** an espresso targeting 40 g finishes at 41.7 g with 2.5× added water configured
- **THEN** the water target remains 100 g

#### Scenario: Repeat an unchanged drink
- **WHEN** another drink starts with the same 40 g target and 2.5× water specification
- **THEN** it reuses 100 g water without recomputing or rewriting the preset merely because a drink started

#### Scenario: Target changes before actual water entry
- **WHEN** an Americano's water request has been made but the device has not entered HotWater and its intended espresso target is edited from 40 g to 42 g
- **THEN** the pending 2.5× water plan may update from 100 g to 105 g
- **AND** actual water entry freezes the effective plan used by both machine control and history

#### Scenario: Standalone request never starts water
- **WHEN** a standalone water request does not enter HotWater
- **THEN** no recipe-water request hold remains latched
- **AND** the existing sequenced-start refusal policy is unchanged

#### Scenario: Yield changes during water dispensing
- **WHEN** the water is already dispensing toward 100 g and the configured espresso target changes to 42 g
- **THEN** that water pour retains its target and the next drink uses 105 g

### Requirement: Ratio water cannot pour with an invalid resolved target
Ratio-water results SHALL be rounded to whole grams and validated against supported weight limits without silent clipping. If the effective espresso target becomes unusable or the result unsupported, recipe water SHALL be unavailable with an explanatory error until corrected. A prior valid cached amount SHALL NOT be used as a fallback. Standalone fixed vessels SHALL remain usable.

#### Scenario: Round consistently
- **WHEN** the espresso target is 41 g and the water ratio is 2.5
- **THEN** added water resolves to 103 g in previews and machine settings

#### Scenario: Target change makes the ratio unusable
- **WHEN** a target change makes ratio water exceed the supported weight limit
- **THEN** starting that recipe's water, including its automatic follow-on, is refused with an explanation
- **AND** correcting the target or ratio makes it available again

### Requirement: Recipe water settings stay isolated from standalone presets
Automatic ratio updates SHALL affect only the active recipe's effective water settings. Selecting a different vessel SHALL keep existing recipe-deactivation behavior. Re-selecting the recipe's own vessel SHALL retain its recipe water specification. Standalone editing SHALL continue operating on fixed preset values even when a recipe uses that vessel by ratio.

#### Scenario: Re-select the recipe's own vessel
- **WHEN** a recipe uses a shared 200 g preset at 2.5× of a 40 g espresso target and the user re-selects that vessel
- **THEN** the recipe stays active and its effective added water stays 100 g

#### Scenario: Select a different standalone vessel
- **WHEN** the user selects a different vessel while a ratio-water recipe is active and no drink is in progress
- **THEN** the recipe deactivates and the selected vessel's fixed settings apply
- **AND** later espresso target changes do not change that standalone vessel's amount

#### Scenario: Saving a preset cannot replace the active recipe plan
- **WHEN** a standalone preset save would overwrite live settings while a recipe owns water
- **THEN** the cached recipe plan remains the effective water setup without recalculating an unchanged ratio
- **AND** the shared preset still stores its explicitly saved fixed values

#### Scenario: Long black retains its completed water step
- **WHEN** a long black has already started its 100 g water step and espresso configuration or recipe settings change
- **THEN** that drink's water capture remains 100 g and its espresso latch is unchanged
- **AND** the latest setup applies after the drink completes

#### Scenario: Select another vessel during an existing drink
- **WHEN** a drink owns a pending or pouring water step and the user selects a different vessel or replaces/deactivates the recipe
- **THEN** the next setup records that selection with the existing deactivation semantics
- **AND** that drink retains its specification and pending plan or captured target until completion or abort
- **AND** the next setup's water settings apply only after the current drink releases them

### Requirement: Equipment-less recipe keeps current equipment
Activation SHALL apply the recipe's own grind and SHALL leave the current equipment selected when the recipe specifies none, without falling back to its bag.

#### Scenario: No equipment in recipe
- **WHEN** a recipe with grind but no equipment is activated
- **THEN** its grind applies and the previously selected equipment remains active
