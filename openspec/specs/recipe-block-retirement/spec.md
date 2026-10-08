# recipe-block-retirement Specification

## Purpose

Retires the `recipe` block from Decenza's profile format. Covers removing the block on write and on sight, the one-time upgrade of saved profiles, promoting a set dose to `recommended_dose`, and a single dose field across every surface, with retired spellings rejected rather than applied.

## Requirements
### Requirement: No recipe block is written

A profile Decenza serializes SHALL NOT contain a `recipe` key, because its editor parameters are reconstructed from the frames on every read. `recipe` SHALL remain listed as a key the serializer models, since that list is the unknown-key passthrough's exclusion list and removing the entry would re-emit stale blocks verbatim.

#### Scenario: A serialized profile carries no block

- **WHEN** any profile is serialized, exported, shared, or uploaded to Visualizer
- **THEN** the emitted JSON contains no `recipe` key

#### Scenario: An existing block is not echoed back

- **WHEN** a profile file that still contains a `recipe` block is loaded and saved
- **THEN** the saved file contains no `recipe` key
- **AND** the block is not preserved by the unknown-key passthrough

#### Scenario: Frames and BLE output are unaffected

- **WHEN** a D-Flow or A-Flow profile is loaded and uploaded to the machine, with and without a
  stored block
- **THEN** the BLE header and every frame are byte-identical in both cases

#### Scenario: A simple profile loses its block without changing

- **WHEN** a `settings_2a` or `settings_2b` profile carrying a block is loaded and saved
- **THEN** the block is gone and every scalar, along with the frames generated from those
  scalars, is unchanged

### Requirement: A stored block is removed on sight

A profile that still carries a `recipe` block SHALL have it removed and the removal persisted, so no profile retains a block once this change has shipped. Removing it in memory alone SHALL NOT be sufficient. Where the profile cannot be written, the block SHALL still be absent from everything the profile produces, and the failed write SHALL NOT prevent the profile from loading.

#### Scenario: A profile imported after the upgrade is stripped

- **WHEN** a profile carrying a block is imported, received as a share code, or synced from
  another device, and then loaded
- **THEN** the block is removed and the stripped profile is written back once
- **AND** loading it again performs no further write

#### Scenario: The write-back is not blocked by the parity gate

- **WHEN** the stripped profile is checked for lossless conversion before being persisted
- **THEN** the removal of `recipe` is not reported as a lost key
- **AND** the write proceeds

#### Scenario: An unwritable profile still loads

- **WHEN** a profile carrying a block cannot be written back
- **THEN** it loads with no block in memory and the failure is reported rather than raised

### Requirement: Removing the block is not a parity loss

The lossless-conversion check that guards stored-encoding upgrade, the `espresso_temperature`
repair, legacy format migration and the profile-sync audit SHALL treat the removal of `recipe` as
deliberate rather than as a lost key.

A structured value is otherwise never inert, so without this the check reports `recipe: KEY LOST`
for any profile still carrying one — permanently disabling those repairs for exactly the profiles
that most need them.

#### Scenario: A profile with a block is still eligible for repair

- **WHEN** a profile carrying a block is checked for lossless conversion against its stripped form
- **THEN** no error is reported
- **AND** stored-encoding upgrade, temperature repair and legacy format migration all proceed

#### Scenario: Other lost keys are still reported

- **WHEN** a conversion drops any key other than `recipe`
- **THEN** that key is still reported as lost

### Requirement: A set dose survives as a recommended dose

`recipe.dose` is the only value in the block that is not reconstructed from the frames or
duplicated by a top-level key. Where a profile carries a dose that differs from the default and
has no explicit recommendation of its own, that value SHALL be preserved as `recommended_dose`,
with `has_recommended_dose` set.

A profile that already carries an explicit recommendation SHALL keep it — the block SHALL NOT
overwrite a value the user set through the editor.

#### Scenario: A user-set dose is promoted

- **WHEN** a profile carrying `recipe.dose` different from the default is read, and the profile
  has no explicit recommended dose
- **THEN** `recommended_dose` takes the block's dose
- **AND** `has_recommended_dose` is set

#### Scenario: A default dose is not promoted

- **WHEN** a profile carrying the default `recipe.dose` is read
- **THEN** no recommendation is enabled on that profile

#### Scenario: An explicit recommendation wins

- **WHEN** a profile carries both an explicit recommended dose and a block dose
- **THEN** the explicit recommended dose is kept unchanged

### Requirement: A dose reported to a caller carries its enabled flag

Where a recommended dose is reported to an external caller, the flag saying whether it is a real
recommendation SHALL be reported with it.

Every profile holds a dose value whether or not one was set — the default is 18 g — so a bare
figure would tell a caller there is a recommendation when there is not.

#### Scenario: Reading a profile with no recommendation

- **WHEN** the parameters of a profile with `has_recommended_dose` unset are requested
- **THEN** the response states that no recommendation is enabled

#### Scenario: Setting a dose through the parameter surface

- **WHEN** a caller sets a dose through the profile parameter surface
- **THEN** the profile's recommended dose takes that value and its recommendation is enabled
- **AND** the field is not reported back as unrecognised or ignored

### Requirement: A dose of zero clears the recommendation

Setting a per-profile dose of zero SHALL disable the recommendation rather than store zero grams. The stored value SHALL be left unchanged, so re-enabling restores the last real dose rather than a default.

#### Scenario: Zero disables rather than recommends

- **WHEN** a caller sets a dose of zero on a profile that has a recommendation
- **THEN** the profile reports that no recommendation is enabled

#### Scenario: The previous value survives being disabled

- **WHEN** a recommendation is disabled by setting zero and later re-enabled
- **THEN** the dose last set is restored, not the default

#### Scenario: A zero dose is absence everywhere
- **WHEN** a de1app `.tcl` profile carries `profile_grinder_dose_weight 0`
- **THEN** the importer treats it as not set, because de1app's Streamline skin writes that key on every save and a zero there never means a deliberate zero

### Requirement: One dose field, whichever surface sets it

Every surface that offers a per-profile dose SHALL read and write the same profile field, `recommended_dose` with `has_recommended_dose`. No surface may keep its own copy.

#### Scenario: An editor's dose control persists

- **WHEN** a dose is set from a recipe editor's Dose control and the profile is reloaded
- **THEN** the control shows the value that was set

#### Scenario: The same dose is visible to every reader

- **WHEN** a dose is set through any one surface
- **THEN** every other surface reporting a per-profile dose reports that same value

#### Scenario: The edit surface takes one name

- **WHEN** a caller sets `dose` on the profile parameter surface
- **THEN** the profile's recommended dose takes that value and its recommendation is enabled

#### Scenario: A retired spelling is reported, not silently applied

- **WHEN** a caller sends `recommended_dose` or `has_recommended_dose` alongside other valid fields
- **THEN** the profile's dose is unchanged
- **AND** the response names the retired field and the replacement, in its message as well as in a
  dedicated field

#### Scenario: A call of nothing but retired spellings fails and changes nothing

- **WHEN** every field in a call is a retired spelling
- **THEN** the response does not report success
- **AND** the loaded profile is not marked modified

#### Scenario: A dose that is not a number is refused

- **WHEN** a caller sends a `dose` that cannot be read as a number
- **THEN** the call fails and the profile's recommended dose and enabled flag are both unchanged

#### Scenario: A dose outside the range is clamped and said so

- **WHEN** a caller sends a `dose` above the accepted maximum
- **THEN** the stored value is the maximum
- **AND** the response reports the adjustment

#### Scenario: Competing spellings are never both discarded

- **WHEN** a caller supplies `dose` and a retired spelling of the per-profile dose in one request
- **THEN** `dose` is applied
- **AND** the response names the retired spelling
- **WHEN** a caller supplies only retired spellings
- **THEN** the response does not report success

The guarantee this scenario has always made — a caller never gets an unchanged profile *and* a
success result — is unchanged. Only the mechanism is: adjudicating a collision between two live
spellings has been replaced by there being one spelling, so the losing side is now reported as
retired rather than as the loser of a conflict.

#### Scenario: An advanced profile takes a dose like any other

- **WHEN** a caller sets `dose` on a profile with no recipe editor type
- **THEN** the dose is applied and enabled, with no editor-type-specific handling

### Requirement: One dose spelling on the edit surface

The profile parameter edit surface SHALL accept exactly one name for the per-profile dose, `dose`, which sets the value and enables the recommendation. `recommended_dose` and `has_recommended_dose` SHALL NOT be accepted as edit inputs, though reporting still names both.

#### Scenario: Retired spellings are not edit inputs
- **WHEN** an edit call sends `recommended_dose` or `has_recommended_dose`
- **THEN** the spelling is not applied as an edit input

### Requirement: Retired spellings are reported as retired

A retired spelling SHALL be reported as RETIRED rather than unrecognised, naming its replacement in the response's human-readable message and not only in a sibling field. A call whose only inputs were retired spellings SHALL change nothing and SHALL NOT report success.

#### Scenario: Retired-only call reports failure and dirties nothing
- **WHEN** an edit call carries only retired spellings
- **THEN** nothing changes, the profile is not marked modified, and the response does not report success

### Requirement: The dose input is validated, not coerced

A dose value that cannot be read as a number SHALL be rejected, never coerced to zero, since zero clears the recommendation. A value outside the accepted range SHALL be clamped, and the adjustment SHALL be reported rather than the caller's number echoed back as stored.

#### Scenario: Malformed dose leaves the profile untouched
- **WHEN** a dose argument cannot be read as a number
- **THEN** the call is rejected and the stored dose and its enabled flag are unchanged

### Requirement: A one-time upgrade brings saved profiles to the new shape

A one-time pass SHALL rewrite already-saved profiles in the user, downloaded and SAF stores to
remove the `recipe` key, promoting a set dose by the rule above. The pass SHALL run once and
SHALL report what it changed and what it skipped.

It SHALL NOT rewrite a profile whose conversion would lose anything other than the block, and it
SHALL run in an order that does not defeat the lossless-conversion gate protecting legacy profile
migration.

#### Scenario: Saved profiles lose their blocks

- **WHEN** the upgrade runs over a store containing profiles with recipe blocks
- **THEN** each profile is rewritten without a `recipe` key
- **AND** each profile's frames, targets and every other key are unchanged

#### Scenario: The upgrade runs once

- **WHEN** the upgrade has already completed
- **THEN** a subsequent start does not rewrite profiles again

#### Scenario: A legacy profile is not rewritten unaudited

- **WHEN** the store holds a legacy-format profile carrying a block
- **THEN** it is converted once through the audited path, not rewritten twice by two passes

#### Scenario: A failed write does not lose a profile

- **WHEN** rewriting a profile fails
- **THEN** the original file is left intact and the failure is reported

#### Scenario: An interrupted write does not lose a profile

- **WHEN** a rewrite is interrupted partway — a full disk, a lost volume, a killed process
- **THEN** the profile that was there before is still there afterwards
- **AND** the upgrade has not recorded that profile as done

