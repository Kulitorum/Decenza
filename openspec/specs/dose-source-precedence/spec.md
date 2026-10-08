# dose-source-precedence Specification

## Purpose
Defines which source supplies the dose for the next shot (recipe, then bag, then profile), when that ladder is trusted to answer, how a profile load and a Brew Settings dose edit interact with it, and which source a dose edit writes to.

## Requirements
### Requirement: The dose for the next shot resolves recipe → bag → profile
The dose for the next shot SHALL be taken from the active recipe if one is active, otherwise from the active bag if one is active, otherwise from the active profile. The ladder SHALL be the same one `yield-anchor` defines for yield. It SHALL be enforced explicitly wherever a dose is resolved or applied, never left to emerge from signal arrival order. A source that holds no dose SHALL be skipped, not treated as zero.

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
A source's id is selected synchronously, but the row saying what dose it designs arrives from a storage worker. Until then the source looks like one that designs no dose. The ladder SHALL therefore report itself unresolved while any active source's row is outstanding. A caller that would write a dose off the ladder's answer SHALL decline while it is unresolved.

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

### Requirement: The ladder is resolved where the dose is written
The ladder SHALL be resolved where the dose is actually written, not where the write was scheduled. A deferred write SHALL NOT rely on a check made when it was armed.

#### Scenario: A deferred write re-checks the ladder
- **WHEN** a dose write is armed and then the active bag changes before the write runs
- **THEN** the write resolves the ladder against the bag that is active when it lands

### Requirement: A profile write is guarded because it is destructive
The profile's dose write is destructive: it writes through to the active bag's stored dose and stamps the active recipe's. A resolution taken too early SHALL NOT be allowed to erase what the bean or recipe remembered.

#### Scenario: A profile write waits for unresolved rows
- **WHEN** the ladder is unresolved and a profile with a recommended dose is loaded
- **THEN** no write reaches the active bag's or recipe's stored dose

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
A dose edit in Brew Settings SHALL write through to the active bag and stamp the active recipe, so it lands on whichever of the top two rungs the ladder names. It SHALL NOT write the active profile's recommended dose.

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

### Requirement: The profile is excluded from dose writes
The profile SHALL be excluded from dose writes, because writing it marks the profile modified and Brew Settings commits on every OK. A dose dialed with neither a recipe nor a bag active SHALL live in session state (`Settings.dye`), and the profile keeps its own recommendation.

#### Scenario: Session dose and profile recommendation can differ
- **WHEN** neither a recipe nor a bag is active and the user dials a dose that differs from the profile's recommendation
- **THEN** the session dose holds the dialed value and the profile's recommendation is unchanged

### Requirement: A source claims a dose only when its write persisted
A source SHALL claim the dose rung only for a dose its row actually persisted. A rung standing on a value no row holds SHALL NOT suppress the profile's dose.

#### Scenario: Unpersisted dose is not claimed
- **WHEN** a dose is dialed against an active source whose storage is unavailable
- **THEN** that source does not claim the rung and the profile's dose is not suppressed

### Requirement: A profile-less recipe never claims the dose rung
A profile-less recipe, such as a hot-water tea, SHALL NOT claim the dose rung. A tea's leaf dose is not a shot dose, and no later edit may promote it onto the rung.

#### Scenario: Tea dose edit does not claim the rung
- **WHEN** a profile-less tea recipe is active and a dose is dialed
- **THEN** the recipe does not become the dose owner

