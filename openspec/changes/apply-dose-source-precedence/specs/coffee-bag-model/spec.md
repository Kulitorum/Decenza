## MODIFIED Requirements

### Requirement: Active bag selection
The system SHALL maintain a single global `activeBagId` in `SettingsDye` (replacing the `bean/selectedPreset` index). The active bag's fields drive the next shot's bean snapshot.

Applying a bag's **yield spec** SHALL be gated on **no recipe being active**: the resolution ladder of `yield-anchor` (recipe → bag → profile) SHALL be enforced explicitly, never left to emerge from the order in which the bag-selection and recipe-activation signals happen to arrive. **Applying a bag's dose SHALL be gated the same way**, per the ladder in `dose-source-precedence`. The two were previously asymmetric — the yield spec respected the ladder while the dose applied unconditionally, so selecting a bag replaced an active recipe's dose while leaving its yield alone. Nothing about the bag justified the difference; the dose simply had no ladder to obey.

#### Scenario: Bag selection applies all fields
- **WHEN** the user selects a bag (from inventory or Change Beans dialog)
- **THEN** all bag fields SHALL become the active state for the next shot

#### Scenario: Bag selection applies dose and yield spec to the machine
- **WHEN** a bag with a stored `doseWeightG` and a yield spec whose mode is not `none` is selected, and no recipe is active
- **THEN** the dose SHALL drive the next shot's dose (`dyeBeanWeight`)
- **AND** switching the bean SHALL first reset the brew overrides to the active profile's defaults, then re-apply the bag's yield spec to the session anchor — so the next shot's target is the bean's own, and a bag without an anchor stays at the profile default
- **AND** the bag's yield spec is NOT routed through `dyeDrinkWeight` (which remains plain DYE drink-weight metadata)

#### Scenario: Recipe-driven bag selection does not overwrite the recipe's dose
- **WHEN** a bag with a stored `doseWeightG` is selected while a recipe supplying a dose is active
- **THEN** the next shot's dose SHALL remain the recipe's
- **AND** the bag's dose SHALL NOT be written to `dyeBeanWeight`

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
