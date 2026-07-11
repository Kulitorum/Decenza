# recipe-model Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
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

### Requirement: Steam block with pitcher snapshot
A recipe's steam block SHALL contain: `hasMilk` (bool), milk weight (g), a pitcher snapshot (name and volume copied by value — never a reference into the global pitcher preset list), and steam temperature, flow, and timeout values.

#### Scenario: Pitcher preset edited after recipe creation
- **WHEN** the user reorders, edits, or deletes entries in the global steam pitcher presets after a recipe was saved
- **THEN** the recipe's steam behavior is unchanged, because the pitcher was snapshotted by value

### Requirement: Recipe lifecycle mirrors bags
A recipe with zero shots SHALL be hard-deletable. A recipe that any shot references SHALL only be archivable: archived recipes disappear from pickers and quick-select but remain readable so shot history provenance never dangles.

#### Scenario: Deleting an unused recipe
- **WHEN** the user deletes a recipe no shot references
- **THEN** it is removed permanently

#### Scenario: Archiving a used recipe
- **WHEN** the user archives a recipe that has shots
- **THEN** it leaves all pickers and the MRU pills, and its shots still display its name

### Requirement: Shots record recipe provenance and steam snapshot
The `shots` table SHALL gain a nullable `recipe_id` recording which recipe (if any) was active at shot start, and SHALL record a snapshot of the steam spec and the hot-water spec in effect, so that promoting any shot to a recipe round-trips the whole drink. Existing rows SHALL be unaffected (nullable columns, single forward migration).

#### Scenario: Shot pulled with a recipe active
- **WHEN** a shot is pulled while a recipe is active
- **THEN** the shot row stores that recipe's id, the steam spec used, and the hot-water spec used

#### Scenario: Legacy shots
- **WHEN** shots recorded before this change are read
- **THEN** they load normally with no recipe provenance and no steam or hot-water snapshot

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

### Requirement: Recipes carry a drink type
The `recipes` table SHALL gain a `drink_type` TEXT column (values: `espresso`, `filter`, `americano`, `long_black`, `latte`, `tea`, `tea_hotwater`), added by migration with kCols registration, riding transfer/backup import like other recipe columns. The value records user intent and SHALL NOT drive machine behavior — activation reads only the blocks and profile. For rows without a stored value (pre-migration recipes) and for promote-from-shot, the type SHALL be derived from the blocks and profile beverage type (hot-water order "after" → americano, "before" → long black; milk → latte; profile filter/pourover → filter; profile tea_portafilter → tea; hot-water block without profile → tea_hotwater; else espresso), and the derived value SHALL be stored on the next save.

#### Scenario: Legacy recipe derives its type
- **WHEN** a pre-migration americano recipe (hot-water block, order "after") is opened for edit
- **THEN** the summary shows drink type Americano, and saving stores `americano`

#### Scenario: Drink type never gates activation
- **WHEN** a recipe's blocks contradict its stored drink type
- **THEN** activation applies the blocks exactly as stored

### Requirement: Recipes link a specific bag
A recipe SHALL link a specific bag via a `bag_id` column (kCols-registered, CREATE TABLE + migration step, riding transfer/backup import with id remapping like `equipment_id`). Bean identity fields (Bean Base canonical id, roaster, coffee) SHALL be retained on the recipe as a display fallback and as the matching key for automatic relinking (see `recipe-bag-lifecycle`). Activation SHALL use the linked bag directly — no most-recently-used resolution. A recipe MAY still have no bag at all (bean-less recipes per the optionality ladder).

#### Scenario: Two open bags of the same bean
- **WHEN** two recipes link two different open bags of the same bean and each is activated in turn
- **THEN** each activation selects exactly its own linked bag and inherits that bag's grind

#### Scenario: Bean-less recipe unaffected
- **WHEN** a recipe with no bag link is activated
- **THEN** the active bag is unchanged and recipe-local grind applies, exactly as before

### Requirement: Existing recipes migrate to bag links
A one-time forward migration SHALL populate `bag_id` for existing recipes by resolving each recipe's bean identity to the current open bag (canonical id first, else case-insensitive roaster+coffee, most recently used first — the previous resolver's logic, run once). Recipes whose bean has no open bag SHALL migrate with no bag link and present as stale.

#### Scenario: Migration resolves the open bag
- **WHEN** the database migrates with a recipe whose bean has one open bag
- **THEN** the recipe's `bag_id` points at that bag and behavior is unchanged from the user's perspective

#### Scenario: Migration with no open bag
- **WHEN** the database migrates with a recipe whose bean has no open bag
- **THEN** the recipe migrates without a bag link and shows the bag-finished state until relinked

### Requirement: Recipe-owned grind
Grind SHALL always live on the recipe: every recipe of a grind-bearing drink type (whether or not it has a linked bag) stores its own `grindPinned` (free-form text, stored opaquely) and optional `rpmPinned`; grind-less drink types (tea, hot-water tea) store none, exactly as today. There is no bag-inherit mode: a recipe's own grind is never read *from* the bag at activation time. Editing grind while a recipe is active writes immediately to that recipe's own fields, and — independently, per `coffee-bag-model`'s "Bean/grinder edits write through to the active bag" — the same edit also writes immediately to the linked bag, since the bag always mirrors the most recently dialed grind regardless of what's driving it. A recipe with no linked bag stores grind/rpm locally exactly as before (unaffected by this change).

#### Scenario: Editing grind on an active recipe updates the recipe and the bag together
- **WHEN** a recipe is active and the user edits its grind or rpm
- **THEN** that recipe's own `grindPinned`/`rpmPinned` change immediately
- **AND** the linked bag's stored grind/rpm are also updated immediately to the same value (coffee-bag-model)

#### Scenario: Sibling recipes on the same bag are independent
- **WHEN** two recipes are linked to the same bag and one recipe's grind is edited
- **THEN** the other recipe's own `grindPinned`/`rpmPinned` is unchanged, even though the shared bag's stored grind just changed — no recipe ever reads its grind from the bag

#### Scenario: Bag re-dial does not affect any recipe's grind
- **WHEN** the linked bag's stored grind changes (via a live edit while no recipe governs it, a different recipe's edit, or a manual bag edit)
- **THEN** no *other* recipe's own `grindPinned`/`rpmPinned` changes as a result

### Requirement: New-recipe grind defaults from the bag, once
When a recipe is created with a linked bag, the bag's current `grinderSetting`/`rpm` SHALL be read exactly once, at creation, to supply the recipe's grind default. On the wizard this is an **editable default offered in the field** — not silently copied and not a live link; the user SHALL be free to accept it as-is or change it before saving, and whatever is on the field at save time becomes the recipe's own stored value, permanently independent of the bag from that point on. On non-interactive create surfaces (MCP, web), the same rule applies at save time in storage: a create that **omits** grind while linking a bag SHALL adopt the bag's current grind/rpm as the recipe's own value (mirroring the existing bag-link save-time normalization), while a create that supplies an **explicitly empty** grind SHALL store it empty — omission means "use the sensible default", explicit empty means "no grind". Promote-from-shot (wizard and MCP `recipe_create_from_shot`) SHALL default grind/rpm from the **shot's own recorded values** — the exact dial that produced the shot being promoted — not from the bag's current dial. On the wizard, the rpm default SHALL only be offered when the recipe's selected equipment reports grinder rpm capability (the storage-side adoption and the migration backfill copy the bag's rpm verbatim — the equipment gate is a UI concern). This default SHALL NOT re-occur on subsequent views of an already-created recipe; the user's own edits (or lack thereof) are authoritative from that point on.

#### Scenario: New recipe defaults to the bag's current dial
- **WHEN** the user creates a recipe and links a bag whose current grind is "18" with rpm 1200
- **THEN** the recipe's grind field shows "18" and rpm field shows 1200 as editable defaults (if the chosen equipment is rpm-capable)
- **AND** the recipe is saved with whatever is on the field at that point, independent of later bag changes

#### Scenario: User overrides the offered default before saving
- **WHEN** the user creates a recipe, the grind field defaults to the bag's "18", and the user changes it to "20" before saving
- **THEN** the recipe saves with "20" as its own grind — the bag's value was only ever an offered starting point, not something silently written into the recipe

#### Scenario: Rpm does not default for a non-rpm grinder
- **WHEN** the user creates a recipe with equipment whose grinder does not report rpm
- **THEN** the rpm field has no default and is not shown

#### Scenario: Editing an existing recipe does not re-offer the default
- **WHEN** the user reopens an existing recipe's details for editing
- **THEN** the grind/rpm fields show the recipe's own stored values, not a fresh read of the bag's current dial

#### Scenario: MCP create omitting grind adopts the bag's dial
- **WHEN** an MCP or web client creates a recipe linking a bag whose current grind is "18", without providing a grind value
- **THEN** the recipe saves with "18" as its own grind

#### Scenario: Explicitly empty grind is respected
- **WHEN** a create supplies an explicitly empty grind value alongside a linked bag
- **THEN** the recipe saves with no grind — the empty value is not overridden by the bag default

#### Scenario: Promote-from-shot defaults from the shot, not the bag
- **WHEN** the user promotes a shot that was pulled at grind "17", and the linked bag's dial has since moved to "18"
- **THEN** the new recipe's grind defaults to "17" (the shot's recorded value), editable before saving

