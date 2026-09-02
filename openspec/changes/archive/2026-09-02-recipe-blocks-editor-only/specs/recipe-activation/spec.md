## MODIFIED Requirements

### Requirement: Tweaks write through; ingredient swaps deactivate
While a recipe is active, a dose change SHALL write through to the active recipe (no dirty state, matching bag semantics). Grind/RPM changes SHALL write through to the active bag and stamp the recipe's own `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`: grind lives on the recipe, the bag always mirrors the last dial, and a grind-less `tea*` recipe never adopts a grind). Dose and grind/RPM are the ONLY live values that reach the recipe from the dial: they are dial-in measurements of what the user physically did, and there is no other write-through.

Yield and temperature are per-brew **overrides**, not tweaks: a Brew Settings change to yield (Stop-at) or temperature (Temp Delta) SHALL apply only as a `Settings.brew` override for the next brew and SHALL NOT write through to the active recipe. The recipe's `yieldG` / `tempOffsetC` SHALL change only via an explicit "Update Recipe" action; when Update Recipe persists a temperature, it SHALL store the delta between the dialed temperature and the profile's espresso_temperature (the offset), never the absolute value. Accordingly, the `MainController` auto-stamp watchers on `SettingsBrew::brewOverridesChanged` (→ `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ `tempOffsetC`) SHALL be removed.

The steam pitcher and the water vessel are **ingredients**, not tweaks. Selecting a different steam pitcher or a different water vessel while a recipe is active SHALL deactivate the recipe, exactly as changing the bean, the equipment package, or the profile does — the user is no longer making that drink, and the change SHALL apply to the live settings while the recipe's stored block is left untouched. Accordingly, the `MainController` auto-stamp watchers on the steam-pitcher selection, the steam-pitcher presets, the steamed-milk weight, the water-vessel selection, and the water-vessel presets SHALL be removed, and two deactivation watchers SHALL take their place.

Each deactivation watcher SHALL gate on OWNERSHIP first, as the bean, equipment and profile watchers do: a recipe whose steam block names no pitcher owns no pitcher choice and SHALL NOT be deactivated by a pitcher change, and a recipe with no active hot-water block SHALL NOT be deactivated by a vessel change. Re-selecting the pitcher or vessel the recipe itself names SHALL NOT deactivate it. The ownership and divergence questions SHALL each have ONE definition, shared by every site that asks them, in the same place as `Recipe::profileDiverged` — a comparison hand-written per call site is free to drift, and the drift is invisible until a shot is stamped with a recipe it did not run.

Two things SHALL NOT deactivate. Editing, adding, removing or reordering a pitcher or vessel PRESET is not an ingredient swap and SHALL leave the active recipe alone; a recipe's block is a by-value snapshot, not a reference. The steamed-milk weight SHALL NOT deactivate either: it is captured automatically at the end of a steam session, so deactivating on it would drop every milk recipe moments before its own shot is saved, costing that shot its recipe attribution.

Deactivation SHALL NOT undo the selection that triggered it. The recipe pitcher override parks the user's own standing selection on activation and restores it on deactivation, so a deactivation driven by the user's own pitcher pick SHALL first make that pick the standing selection — otherwise the unwind re-selects the parked pitcher and the user's choice disappears.

A recipe's `steamJson` / `hotWaterJson` SHALL change only through an explicit recipe edit (the wizard, `recipe_update` over MCP, or the web recipe form). Because activation re-applies the recipe's stored blocks, re-activating the recipe SHALL restore its pitcher and vessel into the live settings.

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
