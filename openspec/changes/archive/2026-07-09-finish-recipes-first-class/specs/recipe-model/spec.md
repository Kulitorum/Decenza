## MODIFIED Requirements

### Requirement: Recipe entity
The system SHALL store recipes in a `recipes` table in the shot-history database, managed by a `RecipeStorage` class following the `CoffeeBagStorage` patterns (async request/ready signal API, background-thread I/O via `withTempDb`). A recipe SHALL have: a name (required), a profile reference by title with embedded profile JSON fallback (required), an optional bean link, an optional equipment package reference, dose (g), yield target (g), optional temperature override, an optional pinned grind value, a steam block, and an optional hot-water block. Optional structured sub-fields (steam block, hot-water block, pinned grind) SHALL be stored as JSON text columns.

#### Scenario: Minimal recipe is valid
- **WHEN** a recipe is created with only a name and a profile
- **THEN** it saves successfully and can be activated, with no bean, equipment, dose, steam, or hot-water fields required

#### Scenario: Storage runs off the main thread
- **WHEN** any recipe read or write is requested
- **THEN** the database work runs on a background thread and results are delivered to the main thread via a queued signal

### Requirement: Shots record recipe provenance and steam snapshot
The `shots` table SHALL gain a nullable `recipe_id` recording which recipe (if any) was active at shot start, and SHALL record a snapshot of the steam spec and the hot-water spec in effect, so that promoting any shot to a recipe round-trips the whole drink. Existing rows SHALL be unaffected (nullable columns, single forward migration).

#### Scenario: Shot pulled with a recipe active
- **WHEN** a shot is pulled while a recipe is active
- **THEN** the shot row stores that recipe's id, the steam spec used, and the hot-water spec used

#### Scenario: Legacy shots
- **WHEN** shots recorded before this change are read
- **THEN** they load normally with no recipe provenance and no steam or hot-water snapshot

## ADDED Requirements

### Requirement: Hot-water block is an opt-in water-vessel snapshot
A recipe MAY carry an optional hot-water block describing added hot water (enabling drinks such as an Americano — espresso plus added hot water). Hot water is opt-in: `hasWater` (bool) turns it on, and when on the recipe SHALL reference a selected water vessel. The vessel supplies all values — the block SHALL store a snapshot of that vessel copied **by value** (its name, amount as volume ml or weight g per the vessel's mode, temperature, and flow) rather than a reference into the global water-vessel preset list. There SHALL be no separate per-recipe amount/temperature/flow input distinct from the vessel; the vessel is the single source of those values. The block SHALL also carry an `order` — whether the water is added `before` the espresso (a long black) or `after` it (an Americano) — defaulting to `after`. The steam block and the hot-water block SHALL be independent, so a recipe MAY carry either, both, or neither.

#### Scenario: Enabling hot water requires a vessel
- **WHEN** the user turns on added hot water for a recipe
- **THEN** the recipe references a water vessel and the block stores that vessel's amount, temperature, flow, and mode as a by-value snapshot

#### Scenario: Water order distinguishes long black from Americano
- **WHEN** the user sets the hot-water order to `before` (long black) or `after` (Americano)
- **THEN** the recipe's hot-water block records that order and returns it unchanged when the recipe is loaded

#### Scenario: Water vessel preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global water-vessel presets after a recipe was saved
- **THEN** the recipe's hot-water behavior is unchanged, because the vessel was snapshotted by value

#### Scenario: Steam and hot water coexist on one recipe
- **WHEN** a recipe carries both a milk steam block and a hot-water block
- **THEN** both are stored and neither overrides the other
