## ADDED Requirements

### Requirement: The dose for the next shot resolves recipe → bag → profile

Where more than one source holds a dose, the dose used for the next shot SHALL be taken from the
active recipe if one is active; otherwise from the active bag if one is active; otherwise from the
active profile.

This ladder SHALL be enforced explicitly wherever a dose is resolved or applied — never left to
emerge from the order in which recipe-activation, bag-selection and profile-load signals happen to
arrive. It is the same ladder `yield-anchor` defines for the yield, and `coffee-bag-model` already
enforces for a bag's yield spec, applied to the dose.

A source that holds no dose is skipped rather than treated as holding zero.

#### Scenario: A recipe outranks a bag and a profile

- **WHEN** a recipe with a dose is active, alongside a bag with a dose and a profile with a
  recommended dose
- **THEN** the next shot's dose is the recipe's

#### Scenario: A bag outranks a profile

- **WHEN** no recipe is active, a bag with a dose is active, and the profile has a recommended dose
- **THEN** the next shot's dose is the bag's

#### Scenario: The profile is the last resort, not the first

- **WHEN** neither a recipe nor a bag supplies a dose, and the profile has one
- **THEN** the next shot's dose is the profile's

#### Scenario: A source without a dose is skipped, not read as zero

- **WHEN** a recipe is active but holds no dose, and the active bag holds one
- **THEN** the bag's dose is used

### Requirement: The ladder is not answered until every active source's row has been read

A source's id is selected synchronously; the row that says what dose it designs arrives from a
storage worker. Between the two, that source is indistinguishable from one that designs no dose.
The ladder SHALL therefore report itself unresolved while any active source's row is outstanding,
and a caller that would WRITE a dose off the ladder's answer SHALL decline while it is unresolved.

This matters because the profile's write is the destructive one: it writes through to the active
bag's stored dose and stamps the active recipe's, so a resolution taken a beat too early does not
merely show the wrong number — it erases what the bean or the recipe remembered.

For the same reason the ladder SHALL be resolved where the dose is actually written, not where the
write was scheduled. A deferred write that checked the ladder when it was armed would still land
after an intervening bag or recipe selection.

#### Scenario: A profile load between selecting a bag and its row arriving

- **WHEN** a bag is selected and a profile with a recommended dose is loaded before the bag's row
  has been read
- **THEN** the profile's dose is not applied
- **AND** the bag's stored dose is unchanged

#### Scenario: A source restored at launch is unresolved until its row is read

- **WHEN** the app starts with a recipe active
- **THEN** the ladder is unresolved until that recipe's row has been read
- **AND** once read, the recipe holds the rung for the rest of the session without needing to be
  re-activated

#### Scenario: Deactivation needs no row

- **WHEN** the active recipe or bag is cleared
- **THEN** the ladder is immediately resolved, with that rung empty

### Requirement: Loading a profile never overwrites a higher-priority dose

Applying a profile's recommended dose to the active dose SHALL be gated on no recipe and no bag
supplying one.

Before this rule, loading a profile wrote its recommended dose to the active dose unconditionally,
so switching profiles mid-session replaced the active recipe's dose with the profile's. That
inverts the ladder, and it does so silently: nothing in the shot plan distinguishes a dose the
recipe specified from one a profile switch substituted.

#### Scenario: Switching profiles with a recipe active

- **WHEN** a recipe with a dose is active and the user loads a different profile that has a
  recommended dose
- **THEN** the active dose is still the recipe's

#### Scenario: Switching profiles with only a bag active

- **WHEN** a bag with a dose is active, no recipe is active, and a profile with a recommended dose
  is loaded
- **THEN** the active dose is still the bag's

#### Scenario: Switching profiles with nothing else active

- **WHEN** neither a recipe nor a bag is active and a profile with a recommended dose is loaded
- **THEN** the active dose becomes the profile's

#### Scenario: A profile without a recommendation changes nothing

- **WHEN** a profile whose recommendation is not enabled is loaded
- **THEN** the active dose is left exactly as it was, whatever supplied it

#### Scenario: The startup load applies no dose at all

- **WHEN** the profile is loaded at launch
- **THEN** the active dose is left as persisted, whatever the profile recommends

Startup is not a resolution point. The bag and recipe rows load asynchronously, so at launch the
ladder cannot be answered — and it does not need to be: the live dose is already persisted from
the last session, set by whichever source won it then. This is the same rule the yield already
follows on the launch load, where persisted overrides survive.

### Requirement: A dose edit reaches the owning source, and the profile is not one

Editing the dose in Brew Settings SHALL continue to write through to the active bag and stamp the
active recipe, so the edit lands on whichever of the top two rungs the ladder names. It SHALL NOT
write the active profile's recommended dose.

The profile is deliberately excluded. The only way to write it is a call that marks the profile
MODIFIED, and Brew Settings' commit path runs on every OK — so nudging the dose in a dial-in
dialog would dirty the loaded profile and ask to be saved. A profile's recommended dose is stored
design, edited in the profile editors and the MCP parameter surface; Brew Settings dials the
session. The ladder governs which source is *read*, and the top two rungs already have write
targets that persist.

Consequently a dose dialed with neither a recipe nor a bag active lives in session state
(`Settings.dye`), which persists across restarts, and the profile keeps whatever recommendation
it was given. The two can differ, and that is correct: one is what the user is pulling today, the
other is what the profile suggests.

#### Scenario: An edit with a recipe active stamps the recipe

- **WHEN** a recipe is active and the user changes the dose in Brew Settings
- **THEN** the recipe's dose is stamped, as it is today
- **AND** the profile's recommended dose is unchanged

#### Scenario: An edit with only a bag active writes the bag

- **WHEN** no recipe is active, a bag is active, and the user changes the dose in Brew Settings
- **THEN** the bag's stored dose follows, as it does today
- **AND** the profile's recommended dose is unchanged

#### Scenario: An edit with neither active does not dirty the profile

- **WHEN** neither a recipe nor a bag is active and the user changes the dose in Brew Settings
- **THEN** the live dose changes
- **AND** the loaded profile is not marked modified

#### Scenario: Dialing a dose onto a source that had none makes it the owner

- **WHEN** a recipe or bag holding no dose is active and the user dials one
- **THEN** the write-through gives that source the dose
- **AND** the ladder now names it as the owner, so a later profile load does not overwrite it

#### Scenario: A source only claims a dose the write-through actually persisted

- **WHEN** a dose is dialed against an active source whose storage is unavailable, so nothing is
  written to its row
- **THEN** that source does not claim the rung

A rung standing on a value no row holds is the same defect as a stale one, reached from the other
direction: the ladder would suppress the profile's dose in favour of a dose nothing remembers.

#### Scenario: A drink with no shot dose does not claim the rung

- **WHEN** a profile-less recipe (a hot-water tea) is active and a dose is dialed
- **THEN** the recipe does not become the dose owner

A tea's leaf dose is not a shot dose — activation deliberately gives such a recipe an empty rung,
and no later edit may promote it onto one, or it would lock the bag and profile out of a value it
never designs.
