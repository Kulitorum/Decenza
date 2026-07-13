# recipe-model Delta

## MODIFIED Requirements

### Requirement: Recipe entity
The system SHALL store recipes in a `recipes` table in the shot-history database, managed by a `RecipeStorage` class following the `CoffeeBagStorage` patterns (async request/ready signal API, background-thread I/O via `withTempDb`). A recipe SHALL have: a name (required), a profile reference by title with embedded profile JSON fallback (required unless the recipe carries a hot-water block with `hasWater` true, in which case the profile MAY be absent), a drink type, an optional bean link, an optional equipment package reference, dose (g), yield target (g), an optional temperature **offset relative to its profile** (`temp_offset_c`, a signed delta in °C where 0 means "brew at the profile's temperature"), an optional pinned grind value, a steam block, and an optional hot-water block. Optional structured sub-fields (steam block, hot-water block, pinned grind) SHALL be stored as JSON text columns.

The temperature offset SHALL be the stored value — never an absolute temperature recomputed against the profile at display time — so editing the profile's temperature moves the recipe's effective brew temperature with it while the stored offset (and what every editor displays) stays exactly what the user set.

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

#### Scenario: Profile temperature edit does not change the stored offset
- **WHEN** a recipe stores a −3° offset on a 90° profile and the profile's temperature is later saved as 88°
- **THEN** the recipe still stores (and its editor still shows) −3°, and its effective brew temperature becomes 85°

## ADDED Requirements

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
The MCP recipe tools and the ShotServer recipe endpoints (including the web recipe editor) SHALL expose the temperature as `tempOffsetC` — a signed delta in °C, present only when non-zero on read, and accepted as the only temperature field on create/update. The legacy absolute `temperatureOverrideC` field SHALL no longer appear in responses nor be accepted in requests, so no client can silently write an absolute value into a delta column.

#### Scenario: MCP reads a recipe with an offset
- **WHEN** an MCP client fetches a recipe holding a −3° offset
- **THEN** the response contains `tempOffsetC: -3` and no `temperatureOverrideC` key

#### Scenario: Web editor round-trips the offset
- **WHEN** the web recipe editor saves a recipe with `tempOffsetC` = 2
- **THEN** the stored recipe holds offset 2 and re-reading it returns `tempOffsetC: 2`
