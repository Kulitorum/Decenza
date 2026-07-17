# recipe-model Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
