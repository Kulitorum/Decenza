# history-recipe-identity Specification

## Purpose

A shot pulled with a recipe records which recipe it came from. Shot History, Auto
Favorites and the web shot list SHALL surface that fact — the recipe's name and drink
type — and SHALL NOT offer to create a recipe from a shot that already has one.

## ADDED Requirements

### Requirement: Shot History rows show the recipe that made the drink

A Shot History row for a shot with a recipe (`recipe_id > 0`) SHALL show that recipe's
name and a drink-type icon in the row's identity position — where the profile name is
shown on rows without a recipe — and SHALL demote the profile name to the head of the
row's secondary line. A row for a shot with no recipe SHALL keep the profile name in the
identity position and SHALL NOT show a drink-type icon.

The recipe name SHALL NOT replace or suppress the profile: both are visible on a
recipe-driven row.

#### Scenario: Shot pulled with a recipe

- **WHEN** the user views a Shot History row for a shot whose record has a recipe id greater than zero
- **THEN** the row's identity line shows the recipe's drink-type icon followed by the recipe's name
- **AND** the profile name appears at the head of the row's secondary line

#### Scenario: Shot pulled without a recipe

- **WHEN** the user views a Shot History row for a shot with no recipe id
- **THEN** the row's identity line shows the profile name, with no drink-type icon
- **AND** the secondary line begins with the bean

#### Scenario: Recipe with no recorded drink type

- **WHEN** a recipe-driven row's recipe carries no stored drink type (a recipe predating drink-type storage)
- **THEN** the row still shows the recipe name, with the default espresso icon rather than a missing icon

### Requirement: Recipe identity on the row is live-resolved, never snapshotted

The recipe name, drink type and archived state shown on a Shot History row SHALL be
resolved from the current `recipes` row by `recipe_id`, not stored on the shot. This is
safe because a recipe with linked shots can only be archived, never deleted, so the row
always resolves. Resolution SHALL happen in the shot-list query itself, not per row.

#### Scenario: Recipe renamed after the shots were pulled

- **WHEN** a recipe is renamed and the user views Shot History
- **THEN** every row for that recipe shows the new name

#### Scenario: Many recipe-driven rows on screen

- **WHEN** the user scrolls a Shot History list where every visible shot used a recipe
- **THEN** no additional per-row database request is issued; recipe identity arrives with the shot list results

### Requirement: Archived recipes are shown dimmed and announced

A Shot History row whose recipe has been archived SHALL render the recipe name dimmed
relative to an active recipe's name, and SHALL include the archived state in the row's
accessible name so the state is not carried by colour alone. An archived recipe's shots
SHALL still appear in the list and in search results.

#### Scenario: Recipe archived after its shots were pulled

- **WHEN** the user views a Shot History row whose recipe is archived
- **THEN** the recipe name is rendered dimmed
- **AND** a screen reader announcing the row states that the recipe is archived

#### Scenario: Archived recipe's shots remain listed

- **WHEN** a recipe is archived
- **THEN** its shots continue to appear in Shot History and continue to match searches for that recipe

### Requirement: The grind is a labelled metric and is never truncated

The grind (and RPM, when recorded) SHALL render on the Shot History row's metrics line,
beside the dose/yield and duration, and SHALL NOT render on the identity line where a
long bean or profile name can elide it away. It SHALL carry a visible label, because the
value alone ("8.75 · 1500") does not identify itself among the other numbers on that
line. The duration SHALL likewise be labelled. This layout SHALL apply to every row,
whether or not the shot used a recipe.

#### Scenario: Narrow row with a long identity

- **WHEN** a row's profile and bean text is too wide for the available width
- **THEN** the identity text is elided and the grind, on its own line, remains fully visible

#### Scenario: Shot with no recorded grind

- **WHEN** a row's shot has no recorded grind
- **THEN** no grind metric is shown and the remaining metrics close up

#### Scenario: Grind is identifiable

- **WHEN** a row shows a grind value
- **THEN** it is preceded by a label naming it as the grind

### Requirement: Shots made with a recipe do not offer promotion to a recipe

Any surface presenting a control that creates a new recipe from a shot SHALL hide that
control when the shot already references a recipe (`recipe_id > 0`). This applies to
Shot History rows, Auto Favorites rows, and the web shot list, matching the behaviour
Shot Detail already has.

#### Scenario: Recipe-driven shot in Shot History

- **WHEN** the user views a Shot History row for a shot that used a recipe
- **THEN** no promote-to-recipe control is shown on that row

#### Scenario: Recipe-less shot in Shot History

- **WHEN** the user views a Shot History row for a shot that used no recipe
- **THEN** the promote-to-recipe control is shown, unchanged

#### Scenario: Recipe-driven shot in Auto Favorites and on the web

- **WHEN** the user views a recipe-driven shot in Auto Favorites, or in the web shot list
- **THEN** no promote-to-recipe control is shown for that shot

### Requirement: Recipe identity reaches the web and MCP surfaces

The web shot list SHALL show the same recipe name, drink-type icon and archived dimming
as the in-app Shot History row, and SHALL apply the same promote-control suppression.
The MCP `shots_list` tool SHALL expose the shot's recipe id and recipe name, omitting
both when the shot used no recipe.

#### Scenario: Web shot list for a recipe-driven shot

- **WHEN** the user opens the web shot list and a listed shot used a recipe
- **THEN** that shot's card shows the recipe's name and drink-type icon, and offers no promote control

#### Scenario: MCP lists a recipe-driven shot

- **WHEN** an MCP client calls `shots_list` and a returned shot used a recipe
- **THEN** the shot object carries the recipe's id and its name

#### Scenario: MCP lists a recipe-less shot

- **WHEN** an MCP client calls `shots_list` and a returned shot used no recipe
- **THEN** the shot object carries neither recipe field
