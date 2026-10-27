# bean-community-search Specification

## Purpose
For a coffee bag, shows the user's own shot history with that bean beside a summary of the Visualizer community's shots of it, on request, and turns the community's best-liked settings into a starting recipe.

## Requirements

### Requirement: Each coffee bag offers your history and the community's on request
Each coffee bag card in the Beans inventory that has a roaster and coffee name SHALL offer a Suggestions button. It SHALL open a dialog for that bag with the user's own profiles and last grind for the bean and, when community search is on, a summary of Visualizer community shots: counts, the most liked profile with median dose, yield, ratio and time, and the grind when pulled on the user's grinder. Nothing SHALL be shown or fetched until the button is tapped, and tea bags SHALL not offer it.

#### Scenario: Enough liked community shots
- **WHEN** at least three people's shots exist for the bean and a profile family has at least two shots rated at or above the floor
- **THEN** the card shows that profile with its medians, Apply and Create recipe

#### Scenario: Too few to rank
- **WHEN** community shots exist but no profile qualifies
- **THEN** the card lists a few of the shots instead

#### Scenario: Similar-roast grind
- **WHEN** the user's last grind comes from a similar roast rather than this bean
- **THEN** the card labels it as a similar-roast grind

### Requirement: The new-bag window offers the same card, acting on save
The new-bag window SHALL offer a Suggestions button once a coffee's roaster and coffee name are entered, showing the card for that identity as entered at the tap (never searching per keystroke). Apply and Create recipe SHALL there be a single choice — selecting one clears the other, tapping it again clears it — carried out with the new bag's id when the bag is saved. Nothing SHALL happen if the bag is not saved.

#### Scenario: Apply chosen in the new-bag window
- **WHEN** the user selects Apply and saves the bag
- **THEN** the bag is created, then a new recipe for it is created and selected

#### Scenario: Window closed without saving
- **WHEN** the user selects Create recipe and closes the window without saving
- **THEN** no bag, recipe or wizard results

### Requirement: Only liked shots rank a profile
Only shots rated at or above the Minimum enjoyment setting (default 70) SHALL rank a profile or choose its top shot.

#### Scenario: Below-floor family
- **WHEN** a profile's shots are all rated below the floor
- **THEN** it is not suggested

### Requirement: Apply saves a new recipe and selects it
After the user confirms the sheet, Apply SHALL create a new recipe named "<coffee> · <profile>" (numbered when taken) with the suggestion's profile, the bag the dialog was opened for, the active equipment, median dose, median ratio as the yield, the profile's temperature, and the grind only when pulled on the user's grinder, then activate it. It SHALL NOT edit the user's current recipe.

#### Scenario: Repeat Apply
- **WHEN** a recipe named "Gran Bar · D-Flow / Q" already exists
- **THEN** Apply creates "Gran Bar · D-Flow / Q 2"

#### Scenario: Profile not installed
- **WHEN** the profile is not installed
- **THEN** it is fetched from the top shot and stored with the recipe

### Requirement: Create recipe opens the wizard prefilled
Create recipe SHALL open the recipe wizard in create mode with the same values Apply would use; nothing is saved until the user saves.

#### Scenario: Create recipe
- **WHEN** the user taps Create recipe
- **THEN** the wizard opens with the suggestion's profile, dose, ratio and matching grind

### Requirement: The same summary over MCP and the web
`bean_search` with `action=community` SHALL return counts and top profiles with unit-named medians, and the web Beans page SHALL show the top profiles and the Visualizer link.

#### Scenario: MCP community action
- **WHEN** an MCP client calls `bean_search` with `action=community`, `roaster` and `coffee`
- **THEN** it receives counts and up to three profiles with `doseG`, `yieldG`, `ratio`, `timeSec`
