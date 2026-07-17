## Why

The recipes management page (`RecipesPage.qml`) renders every recipe as a card in a single flat grid ordered by most-recent use. Once a user accumulates more recipes than fit on one screen, there is no way to find a specific recipe or reorganize the grid — they must eyeball-scan and scroll. Issue [#1520](https://github.com/Kulitorum/Decenza/issues/1520) asks to search recipes and sort them by coffee, profile, or date used.

## What Changes

- Add a **search field** to the recipes page that filters the visible cards as the user types, matching against the recipe **name**, **coffee/bean** (roaster + coffee name), and **profile title** (case-insensitive, debounced, with an inline clear button).
- Add a **sort control** (a sort-field picker plus an ascending/descending toggle) offering these keys:
  - **Date used** (default — preserves today's "most recent first" behavior)
  - **Date created** — surfaces the existing `recipes.created_at` column (present since the table was created, populated by its SQL DEFAULT) as a read-only field; no new column, no migration
  - **Coffee / bean**
  - **Profile**
  - **Name** (alphabetical)
- Search and sort apply to **both the active grid and the archived grid**.
- Add a **"no matches"** empty state shown when a search filters every card out.
- **Persist** the chosen sort field and direction across sessions (search text stays transient).
- **Deferred / out of scope (documented fast-follow):** a **"Rating of most recent shot"** sort key. Unlike the other keys, it is not present in the recipe map and would require a `shots.enjoyment` correlated subquery in `RecipeStorage::loadInventoryStatic` plus a new `InventoryRecipe` field. This change stays purely client-side; rating sort follows separately.

## Capabilities

### New Capabilities
- `recipe-list-organization`: Searching, sorting, and organizing the recipe cards on the recipes management page, including which fields search matches, the available sort keys and default order, scope over active vs. archived grids, the empty-search state, and persistence of the sort preference.

### Modified Capabilities
<!-- None. This adds a new organization layer over the existing recipe list; it does not change recipe-model, recipe-activation, or card-rendering behavior. -->

## Impact

- **QML (primary):** `qml/pages/RecipesPage.qml` — new search field + sort controls above the card grids; filter/sort applied to the `recipes` / `archivedRecipes` arrays before they feed the `Repeater`s. Mirrors the existing `ShotHistoryPage.qml` search/sort pattern (`StyledTextField` + debounce timer + clear button, `SelectionDialog` sort picker, `SortAscending.svg`/`SortDescending.svg` direction toggle).
- **Settings:** two new persisted properties for sort field + direction, added to the same domain sub-object `ShotHistoryPage` uses for its equivalents (`Settings.network.*`), per the settings-domain rule in CLAUDE.md.
- **Storage:** minimal. The recipe map already carries `name`, `roasterName`, `coffeeName`, `profileTitle`, and `lastUsedEpoch`. Adding the **Date created** key surfaces the existing `created_at` column as a **read-only** `kCols` entry (`createdEpoch`) in `RecipeStorage` — SELECTed and exposed to QML, never written (the SQL DEFAULT owns it); no new column, no migration, no change to the create/update paths.
- **Accessibility:** new interactive controls follow `docs/CLAUDE_MD/ACCESSIBILITY.md` (role, name, focusable, press action).
- **Docs:** update the GitHub wiki Manual recipes section to document search/sort.
- **No BLE, no migration, no MCP/web change.**
