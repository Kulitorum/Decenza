# Spec Delta

## MODIFIED Requirements

### Requirement: Grind and RPM entry semantics SHALL be uniform across surfaces
All surfaces accepting a dial-in SHALL agree on the following by inheriting them from the shared control, not restating them per site. RPM "unset" SHALL be represented as `0`, not an empty string. Soft-keyboard avoidance SHALL be owned once, by the picker, which contains the only text inputs.

#### Scenario: Unset RPM is uniform

- **GIVEN** a bag form and a shot-review form, both with no RPM recorded
- **WHEN** each reads its RPM value
- **THEN** both SHALL represent the unset state as `0`

#### Scenario: Suggestions are available where a value is created

- **GIVEN** observed grind settings exist for the grinder that surface owns
- **WHEN** the user creates a new recipe
- **THEN** those observed settings SHALL be offered

#### Scenario: Keyboard avoidance lives in the picker, not the hosts

- **GIVEN** any adopting surface on a touch device
- **WHEN** the user reaches text entry
- **THEN** the typing SHALL happen inside the picker, whose keyboard avoidance keeps the focused field and the commit actions visible
- **AND** no host surface SHALL require its own grind/RPM keyboard registration

### Requirement: Hosts own the value and grinder identity
The value, its writer and the grinder identity the value belongs to SHALL all be supplied by the host. The control SHALL NOT read or write `Settings.dye` directly, so one control serves the live dial-in, a past shot's value, and a recipe's pinned value. Adopting hosts SHALL NOT retain inline grind or RPM text inputs; all typing SHALL happen inside the picker's text mode.

#### Scenario: Past shot value uses the same control
- **WHEN** a past shot's grind value is edited
- **THEN** the shared control opens with the host's value and grinder identity, not the live dial-in

### Requirement: Adopting surfaces
The adopting QML surfaces SHALL be `BrewDialog`, `PostShotReviewPage` and `RecipeWizardPage`.

#### Scenario: Each adopting surface opens the shared picker
- **WHEN** a grind or RPM value is edited on any of the three surfaces
- **THEN** the shared picker control is used

### Requirement: Each surface resolves against its owning grinder context
The brew bar pill and `BrewDialog` SHALL use the active grinder, since they edit the live dial-in. `PostShotReviewPage` SHALL use the grinder recorded on that shot. `RecipeWizardPage` SHALL use the package selected for that recipe.

#### Scenario: Reviewing an old shot uses that shot's grinder
- **WHEN** an old shot's grind is edited in `PostShotReviewPage`
- **THEN** steps and suggestions resolve against the grinder recorded on that shot

### Requirement: Web grind and RPM inputs SHALL offer the same stepped candidates
The ShotServer shot and recipe edit forms SHALL offer stepped candidate values through a native `<input list>` and `<datalist>` pair, not a free-standing text input. Free text SHALL remain accepted, so values outside the candidate list, including non-numeric notations, SHALL still be enterable and saved. The markup SHALL be produced by a shared helper, not inlined per page.



#### Scenario: RPM field hidden for a non-RPM grinder on the web
- **WHEN** a web recipe uses a grinder without RPM support
- **THEN** its RPM control is hidden

#### Scenario: Candidates offered on the web

- **GIVEN** a grind value with a derivable step
- **WHEN** the shot or recipe edit form is rendered
- **THEN** the grind input SHALL offer the stepped candidates as a `<datalist>`

#### Scenario: Web candidates resolve against the record's grinder

- **GIVEN** a shot recorded on a different grinder than the currently active one
- **WHEN** its web edit form is rendered
- **THEN** the offered candidates SHALL derive from the shot's grinder, not the active one

#### Scenario: Free text still accepted on the web

- **WHEN** the user types a grind value absent from the candidate list
- **THEN** the form SHALL accept and save it unchanged

#### Scenario: Shared helper, not per-page markup

- **WHEN** the two forms render their grind inputs
- **THEN** the markup SHALL come from one shared helper in the common ShotServer layer

### Requirement: Candidates come from a dedicated endpoint
Candidates SHALL be served by a dedicated `GET /api/grind-candidates` endpoint taking the grinder identity as parameters, not embedded per record in list payloads. The page SHALL re-request when the selected equipment changes, so candidates follow the grinder the value will be ground on.

#### Scenario: Changing equipment refreshes candidates
- **WHEN** the user changes the selected equipment in a recipe dialog
- **THEN** the page re-requests candidates for the new grinder

## ADDED Requirements

### Requirement: Committing an empty grind clears the value
Committing Done with an emptied grind or RPM field SHALL clear the stored value (grind to empty, RPM to `0`) on EVERY host, with no exceptions. The picker SHALL treat an empty commit as explicit input, which SHALL NOT be silently discarded.

#### Scenario: Explicitly cleared recipe grind remains empty
- **WHEN** a user clears and commits a recipe grind
- **THEN** the recipe stores an empty grind without adopting the bag dial



#### Scenario: The live dial-in is clearable from the pill

- **GIVEN** the brew-bar pill's picker in text mode, with an RPM set
- **WHEN** the user empties the RPM field and commits
- **THEN** the live dial-in's RPM SHALL be cleared to `0`
- **AND** the pill SHALL stop showing the RPM half

#### Scenario: Bag editor has no grind field
- **WHEN** a bag is edited
- **THEN** there is no bag grind field to clear

### Requirement: Blank remains a valid recipe or shot grind
Blank SHALL remain a valid recipe or shot grind value and SHALL NOT inherit a bag dial.

#### Scenario: Explicitly empty recipe grind stays empty
- **WHEN** a recipe is saved with an explicitly empty grind
- **THEN** its grind stays empty

### Requirement: Web candidates use the edited record grinder
Candidates SHALL be computed server-side by the existing C++ stepping machinery, resolved against each record's own grinder (the shot's and the recipe's selected package), never the active one. The stepping logic SHALL NOT be reimplemented in JavaScript.

#### Scenario: Candidates for a recipe follow its equipment
- **WHEN** a recipe's candidates are requested
- **THEN** they are computed against its selected package

## REMOVED Requirements

### Requirement: Committing an emptied grind SHALL clear the value where blank is meaningful
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Committing an empty grind clears the value.

### Requirement: Blank is a valid stored grind value
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Blank remains a valid recipe or shot grind.

### Requirement: Web candidates are computed server-side per record
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Web candidates use the edited record grinder.
