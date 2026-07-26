# recipe-block-retirement Specification

## Purpose
TBD - created by archiving change replace-recipe-block-with-recommended-dose. Update Purpose after archive.
## Requirements
### Requirement: No recipe block is written

A profile Decenza serializes SHALL NOT contain a `recipe` key. Editor parameters for D-Flow and
A-Flow profiles are reconstructed from the frames on every read, so persisting them stores a copy
that no reader consults and that drifts from the frames it duplicates.

`recipe` SHALL remain listed as a key the serializer models. That list is the unknown-key
passthrough's exclusion list, so a listed key is dropped while an unlisted one is captured and
re-emitted verbatim — removing the entry would preserve every stale block permanently, the
opposite of the intent.

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

Encountering a profile that still carries a `recipe` block SHALL remove it and persist the
removal, so that no profile retains a block once this change has shipped. The one-time upgrade
covers what is already stored; this covers everything that arrives afterwards — an import, a
share code, a device-to-device sync, a restored backup.

Removing the block in memory alone is NOT sufficient: a profile the user never re-saves would
keep its block on disk indefinitely.

Where the profile cannot be written — a bundled resource, a read-only store — the block SHALL
still be absent from everything the profile produces, and the failure to persist SHALL NOT
prevent the profile from loading.

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

Setting a per-profile dose of zero SHALL disable the recommendation rather than store a
recommendation of zero grams. The stored value SHALL be left as it was, so re-enabling restores
the last real dose rather than a default.

Zero is how the absence of a dose is expressed everywhere else this value travels: the `.tcl`
importer reads de1app's `profile_grinder_dose_weight 0` as "not set" — de1app's Streamline skin
writes the key on every save, so a zero there never means a deliberate zero — and a recommendation
of zero grams would otherwise reach the dialing context, the AI advisor and any ratio arithmetic
as if it were real.

#### Scenario: Zero disables rather than recommends

- **WHEN** a caller sets a dose of zero on a profile that has a recommendation
- **THEN** the profile reports that no recommendation is enabled

#### Scenario: The previous value survives being disabled

- **WHEN** a recommendation is disabled by setting zero and later re-enabled
- **THEN** the dose last set is restored, not the default

### Requirement: One dose field, whichever surface sets it

Every surface that offers a per-profile dose SHALL read and write the same profile field. No
surface may keep its own copy.

This is what the retired `recipe` block got wrong: it stored a second dose that the editors wrote
and nothing else read, so the value shown in an editor and the value the advisor saw could differ
without either being wrong. The Dose controls on the recipe editors, the advanced editor's
control, the parameter surface, the dialing context and the advisor now all resolve to
`recommended_dose` / `has_recommended_dose`.

**One field, one spelling.** The profile parameter edit surface SHALL accept exactly one name for
the per-profile dose — `dose`, which sets the value and enables the recommendation. The
`recommended_dose` and `has_recommended_dose` spellings SHALL NOT be accepted as edit inputs;
reporting still names both, because a reader needs the flag (see "A dose reported to a caller
carries its enabled flag").

This replaces the earlier rule that two spellings reaching one call must not both be discarded in
silence. That rule assumed both spellings would stay. Keeping them was the mistake: they had
different semantics — `dose` set-and-enable, `recommended_dose` set-only — so resolving a
collision toward the "canonical" spelling stored a dose with the recommendation left disabled, a
state no reader acts on. Removing the second spelling removes the collision rather than adjudicating
it.

A retired spelling is reported as RETIRED rather than merely unrecognised: it is not a typo, and
"unrecognised" alone sends the caller hunting for one instead of telling them what replaced it. The
report SHALL name the replacement, and SHALL appear in the response's human-readable message rather
than only in a sibling field — a client that reads the message and the success flag must not be
told a clean "updated" for a call that dropped an argument.

A call whose only inputs were retired spellings SHALL change nothing and SHALL NOT report success.
Reporting success there is not merely inaccurate: the surface's commit path marks the loaded profile
modified on the way out, so a fully rejected edit would dirty the profile and then invite the caller
to save the modification it never made.

**The dose input is validated, not coerced.** A value that cannot be read as a number SHALL be
rejected. `dose` is read directly as a number, and a failed read yields zero — which is the value
that CLEARS the recommendation, so silently coercing would delete a profile's dose on a malformed
call and report success. A value outside the accepted range is clamped, and the adjustment is
reported rather than the caller's number being echoed back as if stored.

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

