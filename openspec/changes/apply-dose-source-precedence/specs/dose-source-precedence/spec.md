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
