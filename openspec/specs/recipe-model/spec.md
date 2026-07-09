# recipe-model Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipe entity
The system SHALL store recipes in a `recipes` table in the shot-history database, managed by a `RecipeStorage` class following the `CoffeeBagStorage` patterns (async request/ready signal API, background-thread I/O via `withTempDb`). A recipe SHALL have: a name (required), a profile reference by title with embedded profile JSON fallback (required), an optional bean link, an optional equipment package reference, dose (g), yield target (g), optional temperature override, an optional pinned grind value, and a steam block. Optional structured sub-fields (steam block, pinned grind) SHALL be stored as JSON text columns.

#### Scenario: Minimal recipe is valid
- **WHEN** a recipe is created with only a name and a profile
- **THEN** it saves successfully and can be activated, with no bean, equipment, dose, or steam fields required

#### Scenario: Storage runs off the main thread
- **WHEN** any recipe read or write is requested
- **THEN** the database work runs on a background thread and results are delivered to the main thread via a queued signal

### Requirement: Steam block with pitcher snapshot
A recipe's steam block SHALL contain: `hasMilk` (bool), milk weight (g), a pitcher snapshot (name and volume copied by value — never a reference into the global pitcher preset list), and steam temperature, flow, and timeout values.

#### Scenario: Pitcher preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global steam pitcher presets after a recipe was saved
- **THEN** the recipe's steam behavior is unchanged, because the pitcher was snapshotted by value

### Requirement: Bean linking resolves to the current open bag
A recipe SHALL link to a bean (by Bean Base canonical id when available, otherwise by roaster/coffee identity), not to a specific bag. At activation the link SHALL resolve to the current open bag of that bean. When no open bag of the linked bean exists, the recipe SHALL remain usable and surfaces SHALL show a "no open bag of this bean" state rather than an error.

#### Scenario: New bag of the same bean
- **WHEN** a bag is marked finished and a new bag of the same bean is opened
- **THEN** recipes linked to that bean resolve to the new bag without user action

#### Scenario: No open bag
- **WHEN** a recipe's linked bean has no open bag
- **THEN** the recipe can still be activated (profile, dose, steam apply) and the UI indicates the bean is not currently stocked

### Requirement: Grind inherit-or-pin
Grind SHALL remain a bag property by default: a recipe with no pinned grind inherits the linked bean's current bag grind (and rpm) at all times, and the composer SHALL display the inherited values read-only. A recipe MAY override with a pinned grind value (free-form text, stored opaquely) and an optional pinned rpm — the override covers grind and rpm together and SHALL be freely toggleable off (returning to inherit). A recipe with no linked bean SHALL store its grind/rpm locally on the recipe.

#### Scenario: Re-dial updates sibling recipes
- **WHEN** the bag's grind is changed (with or without a recipe active)
- **THEN** every recipe inheriting from that bean reflects the new grind

#### Scenario: Pinned recipe is isolated
- **WHEN** a recipe has a pinned grind and the bag's grind changes
- **THEN** the pinned recipe's grind is unchanged

### Requirement: Recipe lifecycle mirrors bags
A recipe with zero shots SHALL be hard-deletable. A recipe that any shot references SHALL only be archivable: archived recipes disappear from pickers and quick-select but remain readable so shot history provenance never dangles.

#### Scenario: Deleting an unused recipe
- **WHEN** the user deletes a recipe no shot references
- **THEN** it is removed permanently

#### Scenario: Archiving a used recipe
- **WHEN** the user archives a recipe that has shots
- **THEN** it leaves all pickers and the MRU pills, and its shots still display its name

### Requirement: Shots record recipe provenance and steam snapshot
The `shots` table SHALL gain a nullable `recipe_id` recording which recipe (if any) was active at shot start, and SHALL record a snapshot of the steam spec in effect, so that promoting any shot to a recipe round-trips completely. Existing rows SHALL be unaffected (nullable columns, single forward migration).

#### Scenario: Shot pulled with a recipe active
- **WHEN** a shot is pulled while a recipe is active
- **THEN** the shot row stores that recipe's id and the steam spec used

#### Scenario: Legacy shots
- **WHEN** shots recorded before this change are read
- **THEN** they load normally with no recipe provenance and no steam snapshot

