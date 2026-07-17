# recipe-model Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipe entity
The system SHALL store recipes in a `recipes` table in the shot-history database, managed by a `RecipeStorage` class following the `CoffeeBagStorage` patterns (async request/ready signal API, background-thread I/O via `withTempDb`). A recipe SHALL have: a name (required), a profile reference by title with embedded profile JSON fallback (required unless the recipe carries a hot-water block with `hasWater` true, in which case the profile MAY be absent), a drink type, an optional bean link, an optional equipment package reference, dose (g), a **yield spec** (`yield-anchor`), an optional temperature **offset relative to its profile** (`temp_offset_c`, a signed delta in °C where 0 means "brew at the profile's temperature"), an optional pinned grind value, a steam block, and an optional hot-water block. Optional structured sub-fields (steam block, hot-water block, pinned grind) SHALL be stored as JSON text columns.

The yield spec SHALL be stored as `yield_value` (double) + `yield_mode` (`none` | `absolute` | `ratio`) — one value column plus an explicit discriminator, so a recipe can never hold both an absolute yield and a ratio (`yield-anchor`). `mode = none` means the recipe designs no yield of its own and the ladder falls through to the bag, then the profile. The legacy `yield_g` column is converted by migration and left dead in place, mirroring how `temp_override_c` was retired.

The recipe's `doseG` SHALL be a **seed, not a pin**: it seeds the live dose on activation, after which a measured dose supersedes it. A ratio-moded recipe therefore resolves against the recipe's own `doseG` on browsing surfaces (recipe cards), and against the live dose once activated.

The temperature offset SHALL be the stored value — never an absolute temperature recomputed against the profile at display time — so editing the profile's temperature moves the recipe's effective brew temperature with it while the stored offset (and what every editor displays) stays exactly what the user set. The yield spec follows the same principle for the dose: a `ratio` mode is the stored value and the gram target is derived at use time, so a dose change moves the recipe's effective target with it and a stale absolute can never be manufactured.

#### Scenario: Minimal recipe is valid
- **WHEN** a recipe is created with only a name and a profile
- **THEN** it saves successfully and can be activated, with no bean, equipment, dose, steam, or hot-water fields required
- **AND** its yield mode is `none`

#### Scenario: Profile-less hot-water recipe is valid
- **WHEN** a recipe is created with a name and a hot-water block but no profile
- **THEN** it saves successfully and can be activated

#### Scenario: Profile-less recipe without hot water is rejected
- **WHEN** a save is attempted with no profile and no hot-water block
- **THEN** validation fails on every surface (wizard, MCP, web)

#### Scenario: Storage runs off the main thread
- **WHEN** any recipe read or write is requested
- **THEN** the database work runs on a background thread and results are delivered to the main thread via a queued signal

#### Scenario: Profile temperature edit does not change the stored offset
- **WHEN** a recipe stores a −3° offset on a 90° profile and the profile's temperature is later saved as 88°
- **THEN** the recipe still stores (and its editor still shows) −3°, and its effective brew temperature becomes 85°

#### Scenario: Dose change does not change the stored ratio
- **WHEN** a recipe stores `{2.0, ratio}` with a `doseG` of 18 and the live dose becomes 17.5
- **THEN** the recipe still stores (and its editor still shows) `1:2`, and its effective target becomes 35 g

#### Scenario: A recipe cannot hold both an absolute yield and a ratio
- **WHEN** a recipe holding `{36.0, absolute}` is given a ratio of 1:2 from any surface (wizard, Brew Settings, MCP, web)
- **THEN** it holds `{2.0, ratio}` and retains no absolute yield

#### Scenario: A ratio recipe with no dose renders as a bare ratio
- **WHEN** a recipe holds `{2.0, ratio}` and its `doseG` is unset
- **THEN** surfaces that display its yield show `1:2` — there is no dose to derive a gram target from, and no fallback to the profile's target weight

### Requirement: Legacy absolute yields migrate to yield specs
A one-time forward migration SHALL add `yield_value` + `yield_mode` and convert each recipe's legacy `yield_g`: a value greater than 0 becomes `yield_value = yield_g`, `yield_mode = 'absolute'`; 0 or NULL becomes `yield_mode = 'none'`. `yield_g` SHALL be left dead in place rather than dropped, and SHALL no longer be read or written after the migration in normal operation.

Unlike the temperature migration, this conversion needs no profile resolution and cannot fail: an absolute yield is already absolute, so the migration is a relabel, not a recomputation. Every migrated recipe therefore behaves exactly as it did before.

The staged-conversion discipline of the temperature migration SHALL nonetheless apply to device-to-device transfer and backup import: a row that already carries a non-NULL `yield_mode` SHALL import verbatim and its dead `yield_g` SHALL be ignored — reconverting from the dead column would resurrect a yield the user has since changed to a ratio or cleared.

#### Scenario: Legacy absolute yield migrates to an absolute spec
- **WHEN** a recipe row predating this change holds `yield_g` = 36
- **THEN** after migration it holds `yield_value` = 36, `yield_mode` = `absolute`, and behaves exactly as before

#### Scenario: Legacy unset yield migrates to none
- **WHEN** a recipe row predating this change holds `yield_g` = 0 or NULL
- **THEN** after migration it holds `yield_mode` = `none` and falls through the ladder exactly as an unset yield does today

#### Scenario: Import from a legacy-version device converts
- **WHEN** recipes are imported from a source database that has `yield_g` but no `yield_mode` column
- **THEN** the conversion runs on the imported rows, producing the same specs the local migration would have

#### Scenario: Import from a current-version device never reconverts
- **WHEN** a recipe is imported from a source that has `yield_mode` (its dead `yield_g` still holding a pre-migration absolute), including a recipe the user has since changed to a ratio
- **THEN** the imported recipe keeps its ratio — the dead column is ignored

### Requirement: Recipe surfaces expose the yield spec
Every recipe surface — the wizard, MCP, and the web editor — SHALL present the yield as a three-state choice (nothing, an absolute yield, or a ratio) governed by the anchor rule of `yield-anchor`: the last written of {ratio, yield} is the anchor and the other is shown derived, never blank.

The JSON surfaces (MCP, web) SHALL expose `yieldG` and `yieldRatio` as sparse, mutually exclusive keys, and SHALL reject a request carrying both — loudly, naming the conflict, never silently dropping one. This mirrors the existing loud rejection of the retired `temperatureOverrideC` (`mcptools_recipes.cpp:440-441`, `:538-539`; `shotserver_recipes.cpp:205-206`, `:362-363`).

Because partial updates carry only the keys the caller sends, writing one of the pair SHALL clear the other implicitly — a caller sending `yieldRatio` alone SHALL NOT need an explicit clear of `yieldG`. The tool descriptions SHALL state this cross-field semantic, since a present-keys-only contract otherwise implies the omitted key is preserved.

#### Scenario: MCP rejects both yield keys at once
- **WHEN** a `recipe_create` or `recipe_update` call carries both `yieldG` and `yieldRatio`
- **THEN** the call fails with an error naming the conflict and no partial write occurs

#### Scenario: MCP partial update swaps the anchor cleanly
- **WHEN** a `recipe_update` carries only `yieldRatio` for a recipe currently holding an absolute yield
- **THEN** the recipe holds only the ratio afterwards, with no explicit clear required from the caller

#### Scenario: MCP reads a recipe's yield sparsely
- **WHEN** `recipe_get` returns a recipe holding `{2.0, ratio}`
- **THEN** the response carries `yieldRatio` = 2.0 and omits `yieldG` entirely
- **AND** for a recipe whose mode is `none`, neither key appears

#### Scenario: Web editor's blank yield field does not silently clear a ratio
- **WHEN** the web recipe editor saves a recipe whose anchor is a ratio
- **THEN** it posts `yieldRatio` and omits `yieldG` — it SHALL NOT coerce a blank yield input to `0` and clear the anchor

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

### Requirement: Absolute temperature overrides migrate to offsets
A one-time forward migration SHALL add `temp_offset_c` and convert each recipe's legacy absolute `temp_override_c` into an offset: `offset = stored absolute − the profile's espresso_temperature`, resolving the profile by title with the recipe's embedded profile JSON as fallback. A legacy value of 0 (no override) SHALL migrate to offset 0. When the profile cannot be resolved by either path, the recipe SHALL migrate with offset 0 (no temperature pin) — a delta against an unknown baseline is meaningless. Offsets that round to 0 (|offset| < 0.05 °C) SHALL be stored as 0. The migration SHALL run off the main thread with the other schema migrations, and `temp_override_c` SHALL no longer be read or written after it in normal operation.

Device-to-device transfer and backup import SHALL stage-and-convert exactly the source rows that are still **unconverted**: every row of a legacy-version source (no `temp_offset_c` column, detected from the source's `PRAGMA table_info`), and the NULL-offset rows of a current-version source whose own deferred pass had not completed when it was exported. A **converted** row (non-NULL offset) SHALL import verbatim and its dead `temp_override_c` SHALL be ignored — reconverting from the dead column would resurrect an offset the user has since changed or cleared.

#### Scenario: Legacy absolute converts against its own profile
- **WHEN** the database migrates with a recipe storing `temp_override_c` = 87 whose profile's espresso_temperature is 90
- **THEN** the recipe's `temp_offset_c` becomes −3 and behaves identically to before the migration on an unchanged profile

#### Scenario: Unresolvable profile drops the pin
- **WHEN** the database migrates with a recipe whose profile title matches no profile file and that has no embedded profile JSON
- **THEN** the recipe migrates with offset 0 and activates at whatever temperature its profile-load stage yields

#### Scenario: Tea recipes round-trip through the migration
- **WHEN** a portafilter-tea recipe storing an absolute 80° on an 88° tea profile migrates
- **THEN** it stores offset −8, activation still targets 80°, and the wizard's tea temperature field still shows 80

#### Scenario: Import from a legacy-version device converts
- **WHEN** recipes are imported from a source database that has `temp_override_c` but no `temp_offset_c` column
- **THEN** the conversion pass runs on the imported rows, producing the same offsets the local migration would have

#### Scenario: Import from a current-version device never reconverts
- **WHEN** recipes are imported from a source that has `temp_offset_c` (its dead `temp_override_c` still holding pre-migration absolutes), including a recipe whose offset the user reset to 0 after migrating
- **THEN** the imported recipe keeps offset 0 — the dead column is ignored

#### Scenario: An unconverted row inside a current-version source still converts
- **WHEN** recipes are imported from a source that has `temp_offset_c` but whose deferred conversion never ran (a row with a NULL offset and `temp_override_c` = 87)
- **THEN** the row imports as unconverted and the destination's conversion pass produces the same offset the source's own pass would have — the pin is not flattened to 0

#### Scenario: Promote-from-shot stores an offset
- **WHEN** a shot pulled with an absolute brew temperature override of 87 on a 90° profile is promoted to a recipe
- **THEN** the new recipe stores `temp_offset_c` = −3 (converted at promotion time against the shot's profile)

### Requirement: Tea temperatures are edited absolute, stored as the same offset
Portafilter-tea recipes SHALL store their temperature in the same `temp_offset_c` field with the same delta semantics — there SHALL NOT be a second temperature encoding. Because tea users think in absolute temperatures ("80°", not "profile −8°"), the wizard's tea temperature field SHALL stay absolute and convert at the boundary: it loads as `profile espresso_temperature + offset` (offset 0 shows the profile's own temperature) and saves as `entered − profile espresso_temperature` (equal → 0). When the recipe's profile cannot be resolved, the field SHALL be disabled and the stored offset preserved untouched — the field must never accept input the save path would discard. Activation SHALL need no tea special-case — `profile temp + offset` reproduces the absolute the user entered.

Hot-water tea recipes (profile-less) SHALL store no temperature pin at all: the water vessel is the single source of their temperature (per this spec's hot-water-block requirement), so the wizard SHALL NOT show a separate temperature field for them and its summary SHALL present the vessel's temperature. The migration SHALL drop a legacy hot-water-tea absolute quietly (it was never applied at activation and has no anchor to convert against).

#### Scenario: Editing a migrated tea recipe shows its absolute temperature
- **WHEN** the user opens the details of a tea recipe holding offset −8 on an 88° tea profile
- **THEN** the temperature field shows 80, and saving without touching it keeps offset −8 (no silent loss, no re-encoding)

#### Scenario: Tea save converts the entered absolute
- **WHEN** the user sets a tea recipe's temperature to 75 on an 88° profile and saves
- **THEN** the recipe stores offset −13 and activation targets 75°

#### Scenario: Hot-water tea has no temperature pin
- **WHEN** the user edits a hot-water tea recipe
- **THEN** no separate temperature field is offered; the summary shows the selected vessel's temperature, and the recipe stores offset 0

### Requirement: Recipe surfaces expose the offset
The MCP recipe tools and the ShotServer recipe endpoints (including the web recipe editor) SHALL expose the temperature as `tempOffsetC` — a signed delta in °C, present only when non-zero on read, and accepted as the only temperature field on create/update. The legacy absolute `temperatureOverrideC` field SHALL no longer appear in responses, and a create/update request carrying it SHALL be **rejected with an error naming the replacement and its delta semantics** (never a silent drop) — a pre-rename client writing an absolute into the delta field would corrupt the recipe's temperature.

#### Scenario: MCP reads a recipe with an offset
- **WHEN** an MCP client fetches a recipe holding a −3° offset
- **THEN** the response contains `tempOffsetC: -3` and no `temperatureOverrideC` key

#### Scenario: Web editor round-trips the offset
- **WHEN** the web recipe editor saves a recipe with `tempOffsetC` = 2
- **THEN** the stored recipe holds offset 2 and re-reading it returns `tempOffsetC: 2`

#### Scenario: Legacy field is rejected loudly
- **WHEN** an MCP or web client sends `temperatureOverrideC` on recipe create or update
- **THEN** the request fails with an error naming `tempOffsetC` and its signed-delta semantics, and no field is written

