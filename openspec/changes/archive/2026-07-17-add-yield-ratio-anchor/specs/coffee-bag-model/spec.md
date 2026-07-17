# coffee-bag-model Delta

## MODIFIED Requirements

### Requirement: CoffeeBag data model
The system SHALL define a `CoffeeBag` value type with the following fields:
- Identity: `id` (int, DB primary key), `roasterName`, `coffeeName`, `roastDate`, `roastLevel`, `beanBaseId` (canonical UUID, nullable), `beanBaseData` (JSON blob, nullable)
- Lifecycle: `frozenDate` (nullable), `defrostDate` (nullable), `storageHint` (nullable string enum: `counter` / `airtight` / `vacuum-sealed` / `fridge` — describes non-frozen storage only; frozen state is determined solely by `frozenDate` being set, never by `storageHint`, so the two cannot disagree), `openedDate` (nullable date — the non-frozen analogue of `defrostDate`: when the current portion started being actively used/exposed to room temperature), `notes` (nullable), `startWeightG` (double, nullable — column retained but UNSURFACED: the UI field was removed as low-value and Visualizer has no equivalent), `inInventory` (bool, default true)
- Last-used grinder/dose: `grinderBrand`, `grinderModel`, `grinderBurrs`, `grinderSetting`, `doseWeightG` (all nullable)
- Yield spec: `yieldValue` (double) + `yieldMode` (`none` | `absolute` | `ratio`) — the bean's **own** yield, a first-class anchor rather than a deviation from the active profile's target weight (`yield-anchor`). `mode = none` means the bag designs no yield and the ladder falls through to the profile. The legacy `yieldOverrideG` column is converted by migration and left dead in place.
- Visualizer sync: `visualizerBagId` (nullable UUID string), `visualizerRoasterId` (nullable UUID string), `visualizerSyncPending` (bool, default false — a bag edit failed to push and awaits retry)

The yield spec SHALL be a local-only field, same as the grinder/dose fields — it SHALL NOT be included in `touchesVisualizerFields()`, so an anchor edit never triggers a bag PATCH. `storageHint` and `openedDate` are local-only for the same reason (`bean-freshness-followup`): neither is included in `touchesVisualizerFields()` and neither is pushed to a Visualizer bag.

> **Merge note (tasks.md 1.11):** this requirement is rewritten wholesale by BOTH this change (the yield spec) and `bean-freshness-followup` (`storageHint`/`openedDate`), so whichever archives second silently deletes the other's fields. The lifecycle fields above are folded in here deliberately — `bean-freshness-followup`'s code already shipped ([#1510](https://github.com/Kulitorum/Decenza/pull/1510)) while its spec archive is still pending, so this delta would otherwise describe a `CoffeeBag` the code does not have. If `bean-freshness-followup` archives AFTER this change, its own delta must likewise fold in `yieldValue`/`yieldMode` or it will revert the bag to `yieldOverrideG`.

The `beanBaseData` blob SHALL be valid **without** a canonical `id`: a manual bag may carry user-entered detail keys (`origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, `link`, `degree`) while remaining unlinked (`isLinked` stays defined solely by a non-empty `id`). A linked blob additionally carries a `canonical` sub-object — the pristine entry snapshot for revert — which consumers of the flat working keys ignore and shot snapshots carry along unchanged.

#### Scenario: Bag creation with full canonical data
- **WHEN** a user creates a bag from a Bean Base canonical result
- **THEN** the bag SHALL store `beanBaseId`, `beanBaseData` (origin, variety, process, harvest, tasting, producer, elevation, canonical_roaster_id), `roasterName`, `coffeeName`, and the user-entered `roastDate`

#### Scenario: Bag creation without canonical data
- **WHEN** a user creates a bag via manual entry
- **THEN** the bag SHALL store only user-entered fields; `beanBaseId` SHALL be null
- **AND** `beanBaseData` SHALL be null unless the user entered bean details, in which case it SHALL carry those keys with no `id`

#### Scenario: Unlinked blob does not read as linked
- **WHEN** a bag's `beanBaseData` carries detail keys but no `id`
- **THEN** `isLinked` SHALL be false and no canonical id SHALL be sent on shot PATCH for it

#### Scenario: A bag's yield spec is never synced to Visualizer
- **WHEN** a bag's yield anchor is changed
- **THEN** `touchesVisualizerFields()` SHALL return `false` for a fields map containing only the yield spec keys, and no network PATCH SHALL be issued

#### Scenario: A bag cannot hold both an absolute yield and a ratio
- **WHEN** a bag holding `{40.0, absolute}` is given a ratio of 1:3 from any surface (Change Beans dialog, Brew Settings, MCP `bag_update`, web bag editor)
- **THEN** it holds `{3.0, ratio}` and retains no absolute yield

### Requirement: Active bag selection
The system SHALL maintain a single global `activeBagId` in `SettingsDye` (replacing the `bean/selectedPreset` index). The active bag's fields drive the next shot's bean snapshot.

Applying a bag's **yield spec** SHALL be gated on **no recipe being active**: the resolution ladder of `yield-anchor` (recipe → bag → profile) SHALL be enforced explicitly, never left to emerge from the order in which the bag-selection and recipe-activation signals happen to arrive. The bag's dose continues to apply unconditionally, as today.

#### Scenario: Bag selection applies all fields
- **WHEN** the user selects a bag (from inventory or Change Beans dialog)
- **THEN** all bag fields SHALL become the active state for the next shot

#### Scenario: Bag selection applies dose and yield spec to the machine
- **WHEN** a bag with a stored `doseWeightG` and a yield spec whose mode is not `none` is selected, and no recipe is active
- **THEN** the dose SHALL drive the next shot's dose (`dyeBeanWeight`)
- **AND** switching the bean SHALL first reset the brew overrides to the active profile's defaults, then re-apply the bag's yield spec to the session anchor — so the next shot's target is the bean's own, and a bag without an anchor stays at the profile default
- **AND** the bag's yield spec is NOT routed through `dyeDrinkWeight` (which remains plain DYE drink-weight metadata)

#### Scenario: A bag's own anchor is a baseline, not an override
- **WHEN** a bag holding `{42.0, absolute}` is active, no recipe is active, and the profile's `target_weight` is 36 g
- **THEN** every surface SHALL render 42 g as the BASELINE — un-highlighted, with no `36.0 → 42.0g` arrow on the Shot Plan — because the bean's yield is its design, not a deviation from the profile (the `yield-anchor` ladder resolves the baseline; a bag's anchor is button-protected and therefore always deliberate)
- **AND** only a per-brew deviation FROM 42 g SHALL highlight, arrowing against the bean's 42 g rather than the profile's 36 g
- **AND** pressing "Update Bag" on a deviation SHALL make the shown value the bean's stored spec, clearing the highlight on every surface

#### Scenario: Recipe-driven bag selection does not overwrite the recipe's anchor
- **WHEN** a recipe holding `{2.0, ratio}` is activated and activation selects the recipe's own linked bag, which holds `{40.0, absolute}`
- **THEN** the session anchor is `{2.0, ratio}`
- **AND** the bag's yield spec is not applied

#### Scenario: A manual bean switch still hands the brew to the bag
- **WHEN** a recipe is active and the user manually changes the active bean
- **THEN** the recipe deactivates (`recipe-activation`), so no recipe is active and the newly selected bag's yield spec applies normally

#### Scenario: New bag with no dose or yield spec yet
- **WHEN** a bag with a null/0 `doseWeightG` and a yield mode of `none` is selected
- **THEN** the current global dose SHALL remain in effect and the brew yield SHALL follow the profile default
- **AND** the bag SHALL adopt the dose on the first edit or shot save, and its yield spec only when the user presses "Update Bag" in brew settings

#### Scenario: No active bag
- **WHEN** no bag is selected (`activeBagId` is null or references a deleted bag)
- **THEN** the bean summary SHALL display "No beans selected" and prompt the user to select a bag

## REMOVED Requirements

### Requirement: Dose/yield-override stamped on shot save

**Reason**: This requirement bundled two fields that belong on opposite sides of the measurement/intent line established by `yield-anchor`. Its dose half is correct and is retained verbatim by the new "Dose stamped on shot save" requirement. Its yield half is removed. It defined the bag's yield as a *deviation from the active profile*, which the ladder replaces with a first-class anchor, and its float-compare against the profile target (`CoffeeBagStorage::yieldOverrideForTarget`) is one of the inferences the stored mode retires. It was also the direct mechanism of a live bug: the bag's dose writes through on every scale capture while its yield lagged, so the stored pair drifted into implying a ratio the user never chose.

Note the yield half had **two** auto-writers, not one — the requirement's own scenario named only the first: `ProfileManager::activateBrewWithOverrides` on every Brew Settings commit (`profilemanager.cpp:444`), **and** `MainController`'s shot-save stamp on every saved shot (`maincontroller.cpp:3193-3207`). Both are removed. The requirement's title ("stamped on shot save") described the second accurately; its scenarios described only the first.

**Migration**: `yieldOverrideG` is converted to `yieldValue` + `yieldMode` by migration and left dead in place; every existing bag keeps the yield it has, now as an explicit `absolute` anchor (or `none`). What disappears is the *silent learning* — both the Brew Settings OK write and the per-shot stamp. After this change the yield reaches the bag only via the explicit "Update Bag" action, mirroring how a recipe's yield reaches it only via "Update Recipe". Once both call sites are gone, `CoffeeBagStorage::yieldOverrideForTarget` has no remaining callers and is removed.

This is a user-visible loss of a convenience that works today: a bean currently learns its yield with no effort. The trade is deliberate — that learning is what produced the drifting dose/yield pair, and it silently re-anchors a bean from what is only a measurement. It needs a manual entry (`tasks.md` §10.5), because the behaviour being removed was never visible in the first place and its absence will not be either.

## ADDED Requirements

### Requirement: Dose stamped on shot save
The system SHALL update the active bag's `doseWeightG` to the shot's actual dose whenever a shot is saved (dose may originate from SAW/profile settings rather than a manual edit).

#### Scenario: Auto-stamp after dial-in adjustment
- **WHEN** a shot is saved with a different dose than the active bag stored
- **THEN** the active bag's `doseWeightG` SHALL be updated to the shot's value with no user prompt

### Requirement: The bag's yield spec is button-protected
The bag's dial memory SHALL split along the measurement/intent line of `yield-anchor`. `grinderSetting`, `rpm`, and `doseWeightG` are dial-in — things the user physically did — and SHALL keep their existing unconditional write-through. The yield spec is design intent and SHALL reach the bag **only** via the explicit "Update Bag" action in Brew Settings (`recipe-aware-brew-settings`).

No other action SHALL write the bag's yield spec: not a shot save, not Brew Settings OK, not a dose capture, and not a bag selection.

#### Scenario: Yield is not stamped on shot save
- **WHEN** a shot is saved at a target that differs from the active bag's stored yield spec
- **THEN** the active bag's yield spec SHALL be unchanged

#### Scenario: Yield is not stamped on Brew Settings OK
- **WHEN** the user dials a yield or ratio in Brew Settings and taps OK without pressing "Update Bag"
- **THEN** the value applies to the session anchor only
- **AND** the active bag's yield spec SHALL be unchanged

#### Scenario: Yield reaches the bag only via Update Bag
- **WHEN** no recipe is active, the user dials a ratio of 1:3 in Brew Settings and taps "Update Bag"
- **THEN** the active bag holds `{3.0, ratio}`

#### Scenario: A dose capture cannot drift the bag's stored pair
- **WHEN** a bag holds `{36.0, absolute}` with a `doseWeightG` of 18 and a dose capture reads 17.5 g
- **THEN** the bag's `doseWeightG` becomes 17.5 and its yield spec stays `{36.0, absolute}`
- **AND** no implicit ratio is derived from or written to the pair

#### Scenario: Grind and rpm write-through are untouched
- **WHEN** the user changes the grinder setting or RPM while a bag is active
- **THEN** the active bag's `grinderSetting`/`rpm` SHALL be updated immediately, exactly as before this change
