## Context

`RecipesPage.qml` renders recipes as `RecipeDrinkCard`s inside a `Repeater` wrapped in a `Flow` (active grid ~lines 500–511, archived ~528–541). The model is two plain JS arrays — `property var recipes: []` and `property var archivedRecipes: []` — populated from `RecipeStorage`'s `inventoryReady(QVariantList)` / `archivedReady(QVariantList)` signals. Each array element is a map already carrying `name`, `roasterName`, `coffeeName`, `profileTitle`, `lastUsedEpoch`, `drinkType`, `shotCount`, and `stale`. The storage query (`loadInventoryStatic`) currently orders `last_used DESC, id DESC`.

`ShotHistoryPage.qml` already implements the search + sort pattern this change mirrors: a debounced `StyledTextField` with an inline clear button, a `SelectionDialog` sort-field picker, an ascending/descending toggle button using `SortAscending.svg` / `SortDescending.svg`, and persistence to `Settings.network.shotHistorySortField` / `shotHistorySortDirection` (declared in `src/core/settings_network.{h,cpp}` and serialized in `src/core/settingsserializer.cpp`).

## Goals / Non-Goals

**Goals:**
- Let a user with many recipe cards find a recipe by typing (name / coffee / profile).
- Let a user reorder the grid by date used, coffee/bean, profile, or name, ascending or descending.
- Persist the sort preference; keep search transient.
- Reuse the proven `ShotHistoryPage` UI vocabulary so the two pages feel consistent.

**Non-Goals:**
- Grouped/sectioned layout (collapsible headers). Flat sort only.
- "Rating of most recent shot" sort — deferred fast-follow; it needs a `shots.enjoyment` subquery and is the only requested key not already in the recipe map.
- Any change to recipe activation, the card component, storage schema, MCP, or web surfaces.

## Decisions

### Client-side filter/sort in QML, not in the SQL query
The inventory is a small in-memory `QVariantList` delivered whole to QML. Filtering and sorting the `recipes` / `archivedRecipes` arrays in QML (a computed `visibleRecipes` derived from search text + sort key + direction) keeps the change entirely in the view layer and re-sorts instantly without a DB round-trip.

- **Alternative — order/filter in `loadInventoryStatic`:** rejected. It would push transient UI state (search text, direction) into the async storage layer and re-query on every keystroke, for no benefit at this list size. The existing `ORDER BY last_used DESC` remains the fallback order the array arrives in.

The derived list is built with a `sort()` over a shallow copy (never mutate the source arrays, so re-filtering on search change is cheap and order-stable). Comparators: `lastUsedEpoch` numeric; `name` / `profileTitle` / bean (`roasterName` + " " + `coffeeName`) via `localeCompare` case-insensitive. Recipes missing a sort field (e.g. bean-less) sort to the end regardless of direction so blanks never float to the top.

### Date created surfaces the existing `created_at` column read-only
The `recipes` table has carried `created_at INTEGER DEFAULT (strftime('%s','now'))` since it was first created (migration 25), and the INSERT — built from the *writable* `kCols` — never touches it, so every existing row already holds a valid creation epoch. To sort by it, add `created_at` as a **read-only** `kCols` entry (`writable=false`, `nullptr` bind, new `COL_EPOCH_RO` macro mirroring `COL_ID`), appended last so `recipeFromQueryRow`'s positional read stays aligned. It is SELECTed and flows into the recipe map (`createdEpoch`) via the generic `toVariantMap`, but is excluded from INSERT and the update map — the SQL DEFAULT remains the sole writer.

- **Alternative — a fully writable column / new migration:** rejected. Nothing should overwrite the creation timestamp, and the column already exists, so neither a schema edit nor a migration is warranted.
- **Trade-off:** transfer/backup import inserts without binding `created_at`, so imported recipes take the *import* time as their creation date. Acceptable — the field was never surfaced before, so there is no regression, and import-time ordering is reasonable.

### Reuse ShotHistoryPage's controls verbatim in spirit
Same `StyledTextField` + `searchTimer` debounce + clear button; same `SelectionDialog` for the sort key; same direction toggle with the `SortAscending.svg`/`SortDescending.svg` icons. This is UI reuse, not code extraction — a shared component is out of scope for one extra page.

### Persist via a new `Settings.network` pair
Add `recipeSortField` (string key) and `recipeSortDirection` (`"ASC"`/`"DESC"`) to `SettingsNetwork`, alongside the existing `shotHistorySort*` pair, and serialize them in `settingsserializer.cpp`. Per the CLAUDE.md settings rule, they live on the domain sub-object, not on `Settings` directly. Defaults: field = date used, direction = `DESC` (preserves current "most recent first"). Search text is not persisted.

- **Alternative — a new settings domain:** rejected; the recipe page prefs are a direct sibling of the shot-history prefs and belong in the same place.

### Control-bar visibility
The search + sort bar is **always shown** (matching ShotHistoryPage), placed above the active grid, below the header row. It stays compact so a light user with few recipes is not burdened. (A count threshold was considered but adds conditional layout state for marginal benefit and would hide the feature exactly when a growing library first needs it.)

### Empty states
Two distinct empty states: the existing "no recipes yet" starter tiles (shown when the library is genuinely empty) remain; a new "no matches" message shows when a non-empty library is filtered to zero cards by the search.

## Risks / Trade-offs

- **[Search across three fields could surprise users** (a query matches on profile when they meant name)] → Acceptable and expected; ShotHistory search behaves the same way and broad matching is the point when hunting a card.
- **[Sort/search must cover the archived grid too, or the two grids drift]** → Apply the same derived-list logic to both `recipes` and `archivedRecipes`; specs require it.
- **[Bean-less / profile-less recipes have blank sort keys]** → Comparator sends blanks to the end in both directions, so they never dominate the top of the list.
- **[Adding the deferred rating key later touches C++]** → Isolated and additive (new subquery + `InventoryRecipe` field + map insert + one more sort key); this design deliberately leaves the door open without pre-building it.

## Migration Plan

No data migration. Two new settings keys default cleanly on first run (date used / DESC = today's behavior), so existing users see no change until they interact with the new controls. Pure additive UI; rollback is removing the control bar and the two settings.

## Open Questions

- None blocking. Whether to also expose "drink type" as a sort key can be revisited after ship if users ask; it is trivially addable under the same mechanism.
