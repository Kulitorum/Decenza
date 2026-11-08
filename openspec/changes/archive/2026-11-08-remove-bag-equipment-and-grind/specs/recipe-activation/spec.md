# Spec Delta

## MODIFIED Requirements

### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline. QML pill taps, the MCP `recipe_activate` tool and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly, with no bean-identity resolution at activation time.

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, the recipe equipment package is selected when present, dose/yield/temperature apply, grind/rpm apply from the recipe's own owned values (recipe-model), and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via QML, MCP, or the web API
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

### Requirement: Stale recipes activate with the finished bag's data
Activating a recipe whose linked bag is finished SHALL apply the full bundle: profile, dose/yield/temperature, equipment, steam, and hot water apply normally, and the recipe's own grind applies. The finished bag SHALL NOT be returned to inventory by activation. Activation SHALL succeed with no error or dialog; the stale indication remains a display concern of the listing surfaces.

#### Scenario: Pulling a shot from a stale recipe
- **WHEN** the user activates a stale recipe and starts espresso
- **THEN** the shot runs with the recipe's profile, numbers, and the recipe's own grind — identical to activation before the bag was finished

## ADDED Requirements

### Requirement: Equipment-less recipe keeps current equipment
Activation SHALL apply the recipe's own grind and SHALL leave the current equipment selected when the recipe specifies none, without falling back to its bag.

#### Scenario: No equipment in recipe
- **WHEN** a recipe with grind but no equipment is activated
- **THEN** its grind applies and the previously selected equipment remains active
