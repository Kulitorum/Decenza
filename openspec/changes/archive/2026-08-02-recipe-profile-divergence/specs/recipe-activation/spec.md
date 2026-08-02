# recipe-activation Specification

## ADDED Requirements

### Requirement: A recipe whose profile is not installed shows that it is
A recipe that names a profile which is not installed SHALL be visibly marked as missing its profile wherever recipes are listed, so that an unactivatable recipe is distinguishable from a working one without tapping it.

The marking SHALL be derived at display time from the loaded profile catalog. It SHALL NOT be stored on the recipe, and no pass over the recipe library SHALL scan, mark, migrate or repair stored recipes. Consequently the marking SHALL apply to every route into the state — the profile deleted, a `profileTitle` that never resolved, a restored database, a device transfer arriving without its profiles — and SHALL clear on its own when the profile becomes available again.

The marking SHALL follow the profile catalog, so a profile deleted or imported while a recipe list is on screen updates that list without it being rebuilt.

A recipe that names no profile SHALL NOT be marked: it is a valid profile-less drink, not a broken one.

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

### Requirement: A failed activation tells the user why
When activation fails, the user SHALL be shown that it failed and what is wrong, rather than only a selection that reverts. When the cause is a profile that is neither installed nor carried as stored JSON, the message SHALL name the missing profile title, because that is the value the user must change to fix it.

#### Scenario: Activating a recipe whose profile is gone
- **WHEN** the user activates a recipe whose profile is not installed and which carries no stored profile JSON
- **THEN** activation fails, the selection reverts, and the user is shown a message naming the missing profile

#### Scenario: The machine is not left half-configured
- **WHEN** activation fails for a missing profile
- **THEN** the previously loaded profile and all brew settings are unchanged

### Requirement: Deleting a profile warns when recipes name it
Deleting a profile that one or more recipes name SHALL warn the user, stating how many recipes use it, and SHALL proceed on confirmation. Deletion SHALL NOT be refused: a user may delete their own profiles.

The affected recipes SHALL NOT be modified — the profile SHALL NOT be snapshotted into them to keep them working, and their stored `profileTitle` SHALL be left as it is. A recipe pointing at a profile the user deleted is missing its profile, and says so; re-pointing it is done in the recipe editor, which already edits the profile a recipe names. No repair action SHALL be offered in the delete flow.

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

## MODIFIED Requirements

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, changes to dose, steam values, milk weight, or the hot-water selection (the chosen water vessel and its values) SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind/RPM changes SHALL write through to the active bag and stamp the recipe's own `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`: grind lives on the recipe, the bag always mirrors the last dial, and a grind-less `tea*` recipe never adopts a grind).

Yield and temperature are per-brew **overrides**, not tweaks: a Brew Settings change to yield (Stop-at) or temperature (Temp Delta) SHALL apply only as a `Settings.brew` override for the next brew and SHALL NOT write through to the active recipe. The recipe's `yieldG` / `tempOffsetC` SHALL change only via an explicit "Update Recipe" action; when Update Recipe persists a temperature, it SHALL store the delta between the dialed temperature and the profile's espresso_temperature (the offset), never the absolute value. Accordingly, the `MainController` auto-stamp watchers on `SettingsBrew::brewOverridesChanged` (→ `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ `tempOffsetC`) SHALL be removed.

Changing the active bag/bean or the equipment package SHALL deactivate the recipe (event-based, no timers); the recipe itself SHALL be unchanged by deactivation.

The active recipe SHALL be deactivated whenever the profile it names ceases to be the loaded profile, **by any route** — the user selecting a different profile, the named profile being deleted, or a restored selection meeting a profile that is not the one it names. The rule SHALL be expressed as a single shared predicate consulted at every such site, not as a comparison written independently at each one.

A recipe that names no profile SHALL NOT be deactivated by any profile change: it owns no profile choice, exactly as a bean-less recipe owns no bag choice and an equipment-less recipe owns no equipment choice.

Editing the loaded profile in place SHALL NOT deactivate the recipe. The recipe names a title; an edit that keeps the title has not changed what the recipe points at.

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
