# recipe-model Specification (delta)

## MODIFIED Requirements

### Requirement: Recipe entity
The system SHALL store recipes in a `recipes` table in the shot-history database, managed by a `RecipeStorage` class following the `CoffeeBagStorage` patterns (async request/ready signal API, background-thread I/O via `withTempDb`). A recipe SHALL have: a name (required), a profile reference by title with embedded profile JSON fallback (required unless the recipe carries a hot-water block with `hasWater` true, in which case the profile MAY be absent), a drink type, an optional bean link, an optional equipment package reference, dose (g), yield target (g), optional temperature override, an optional pinned grind value, a steam block, and an optional hot-water block. Optional structured sub-fields (steam block, hot-water block, pinned grind) SHALL be stored as JSON text columns.

#### Scenario: Minimal recipe is valid
- **WHEN** a recipe is created with only a name and a profile
- **THEN** it saves successfully and can be activated, with no bean, equipment, dose, steam, or hot-water fields required

#### Scenario: Profile-less hot-water recipe is valid
- **WHEN** a recipe is created with a name and a hot-water block but no profile
- **THEN** it saves successfully and can be activated

#### Scenario: Profile-less recipe without hot water is rejected
- **WHEN** a save is attempted with no profile and no hot-water block
- **THEN** validation fails on every surface (wizard, MCP, web)

#### Scenario: Storage runs off the main thread
- **WHEN** any recipe read or write is requested
- **THEN** the database work runs on a background thread and results are delivered to the main thread via a queued signal

## ADDED Requirements

### Requirement: Recipes carry a drink type
The `recipes` table SHALL gain a `drink_type` TEXT column (values: `espresso`, `filter`, `americano`, `long_black`, `latte`, `tea`, `tea_hotwater`), added by migration with kCols registration, riding transfer/backup import like other recipe columns. The value records user intent and SHALL NOT drive machine behavior — activation reads only the blocks and profile. For rows without a stored value (pre-migration recipes) and for promote-from-shot, the type SHALL be derived from the blocks and profile beverage type (hot-water order "after" → americano, "before" → long black; milk → latte; profile filter/pourover → filter; profile tea_portafilter → tea; hot-water block without profile → tea_hotwater; else espresso), and the derived value SHALL be stored on the next save.

#### Scenario: Legacy recipe derives its type
- **WHEN** a pre-migration americano recipe (hot-water block, order "after") is opened for edit
- **THEN** the summary shows drink type Americano, and saving stores `americano`

#### Scenario: Drink type never gates activation
- **WHEN** a recipe's blocks contradict its stored drink type
- **THEN** activation applies the blocks exactly as stored
