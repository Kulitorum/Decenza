# Recipe identity in shot history

## Why

A shot pulled with a recipe records `shots.recipe_id`, but nothing on the Shot History
row says so — the row shows profile, bean and grind, and the recipe the user actually
chose the drink by is invisible. Most users do not keep the wizard's auto-suggested
name (`Bean · Type · Profile`); they name recipes for themselves — "Dad Monday",
"Comp V3", "Weekend milk" — so the recipe name is information that exists **nowhere
else on the row and nowhere in search**. Typing "Dad Monday" into Shot History today
matches zero shots, because that string is absent from `shots` and from `shots_fts`.

Shot Detail already solves this half: it renders a recipe card, live-resolved by
`recipeId` (`shot-recipe-card`), and hides its "create recipe from this shot" button
when the shot already has one. Shot History, Auto Favorites and the web `/shots` list
never got either behaviour.

## What Changes

- **Shot History rows show the recipe.** When `recipe_id > 0`, the row's identity line
  becomes the drink-type icon plus the recipe name (where the profile name sits today),
  and the profile demotes to the head of the second line. Rows with no recipe keep the
  profile in the identity slot.
- **Grind pins right on the second line, for every row.** The second line becomes a
  RowLayout: an elidable identity cell (profile · bean, or bean alone) and a
  never-elided grind cell. This changes existing recipe-less rows — the grind moves out
  of its `(8)` parenthetical to the right edge — and in exchange the grind stops being
  the first thing a long roaster name truncates.
- **Archived recipes render dimmed**, with "archived" carried in the row's accessible
  name so the state is not colour-only.
- **Free-text search finds recipe names.** A bare search term resolves against
  `recipes.name` via a subquery OR'd with the FTS match — the mechanism migration 23
  already established for grinder identity.
- **New `recipe:` keyword**, the first string-valued keyword: `recipe:dad` (single
  token) and `recipe:"dad tuesday"` (quoted, for names sharing a leading word). Both
  are substring matches, scoped to the recipe name only — unlike bare text, which also
  sprays across notes, beans and profile.
- **Tap-through exact filter.** Tapping the recipe name on a row filters history to
  that recipe by id — rename-proof and unambiguous between same-named recipes.
- **The promote-to-recipe button disappears on recipe-driven shots**, on Shot History,
  Auto Favorites and the web `/shots` list — finishing the rollout Shot Detail already
  has.
- **Parity**: the web `/shots` list gains the same recipe name, icon, dimming, search
  and button gating; MCP `shots_list` gains `recipeId` / `recipeName`.

Recipe identity is a **reference, not a snapshot** — no new column, no migration, no
FTS rebuild. Renaming a recipe relabels its history, matching the rule
`shot-recipe-card` already promoted for Shot Detail.

## Capabilities

### New Capabilities

- `history-recipe-identity` — the Shot History row's recipe name, drink-type icon,
  archived dimming, the pinned-grind second line, and the suppression of the
  promote-to-recipe button on shots that already have a recipe.
- `history-recipe-search` — finding shots by recipe: bare free text, the `recipe:`
  keyword in both single-token and quoted forms, and the exact tap-through filter.

### Modified Capabilities

None. `shot-recipe-card` governs the Shot Detail / Shot Review cards and is unchanged;
this change extends its live-resolve rule to new surfaces rather than altering it.

## Impact

- `src/history/shothistorystorage_queries.cpp` — `requestShotsFiltered` gains a
  `LEFT JOIN recipes`, three new selected columns, a recipe-name subquery in the
  free-text clause, and a `recipeId` / `recipeName` term in `buildFilterQuery`.
- `src/history/shothistory_types.h` — `ShotFilter` gains `recipeId` and `recipeName`.
- `src/history/shothistorystorage.h/.cpp` — `requestAutoFavorites` and
  `requestAutoFavoriteGroupDetails` select `recipe_id` so their rows can gate the
  promote button.
- `qml/pages/ShotHistoryPage.qml` — delegate identity line, second-line RowLayout,
  accessible name, promote-button gate, keyword parsing and the Keywords help sheet.
- `qml/pages/AutoFavoritesPage.qml` — promote-button gate.
- `src/network/shotserver_shots.cpp` — web shot card: recipe name, icon, dimming,
  button gate; the list query gains the same join and search clause.
- `src/mcp/mcptools_shots.cpp` — `shots_list` recipe fields.
- `tests/tst_shothistorystorage.cpp` — new test slots (not a new file).
- Wiki manual — Shot History page: the recipe line and the `recipe:` keyword.

No schema migration. `idx_shots_recipe_id` already exists; the join is on the
`recipes` primary key and the query already runs off the main thread.
