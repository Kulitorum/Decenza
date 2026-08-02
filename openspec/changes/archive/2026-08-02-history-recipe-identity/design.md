## Context

`shots.recipe_id` has existed since migration 25, with `idx_shots_recipe_id`. The recipe
NAME has never been on the shot row. `shots_fts` is an external-content FTS5 table
(`content='shots'`), so it can only ever index columns of `shots` — a recipe name cannot
enter it without denormalising.

Shot Detail already resolves recipe identity live by `recipeId` (`RecipeResolver.qml`,
spec `shot-recipe-card`) and already hides its promote-to-recipe button when
`recipeId > 0` (`ShotDetailPage.qml:1130`). Shot History, Auto Favorites and the web
`/shots` list have neither behaviour. So most of this change is finishing a rollout, not
inventing a rule.

The exact precedent for the search half is grinder identity: migration 23 **removed**
`grinder_brand`/`grinder_model` from `shots` and from the FTS table, moved grinder to the
`equipment_id` pointer, and made free-text search resolve it through a subquery OR'd with
the FTS hit (`shothistorystorage_queries.cpp:294-311`). Recipes are the same shape of
thing — a user-owned, renameable, id-referenced entity — so they get the same treatment.

Two facts about naming drove the UI decisions, and neither is obvious from the code:

- The wizard's `suggestName()` composes `Bean · ShortType · Profile`, so a default-named
  recipe repeats almost everything already on the row. **Most users do not keep that
  name.** The design is therefore built for arbitrary names ("Dad Monday"), and the
  stutter a default-named recipe produces is accepted rather than detected.
- An arbitrary name carries information found nowhere else on the row and nowhere in the
  search index — which is why the search half is the larger half of the value.

## Goals / Non-Goals

**Goals:**

- Show which recipe made a drink, on every shot-listing surface.
- Make a recipe's shots findable by name (typed) and by identity (tapped).
- Finish the promote-button gating rollout across all four surfaces.
- No schema migration.

**Non-Goals:**

- Snapshotting recipe identity onto the shot row.
- Changing Shot Detail or Shot Review (`shot-recipe-card` is unchanged).
- Recipe name in DYE / Visualizer upload metadata.
- Sorting or grouping history by recipe.
- Any heuristic that detects an auto-composed recipe name and collapses the row
  differently.

## Decisions

### Reference, not snapshot

Recipe identity is read through `recipe_id` at query time. No `shots.recipe_name` column,
no FTS rebuild, no backfill.

*Why:* a recipe with linked shots can only be archived, never hard-deleted, so the id
always resolves — the invariant `shot-recipe-card` already relies on. Renaming then
relabels all history, which is what a user renaming "V3" to "V3 (final)" wants. A
snapshot would split search results across the rename and would make two same-named
recipes indistinguishable.

*Alternative considered:* denormalised `shots.recipe_name`, which would put the name into
FTS for free. Rejected — it buys one OR'd subquery in exchange for a migration, an FTS
rebuild, trigger changes, and rename/ambiguity bugs.

*Consequence to accept:* a recipe's *content* drifts (its dose today is not the dose the
shot used). This is already handled — the shot's own frozen dose/yield/grind are what the
row and the detail page display; only the name and drink type are live.

### Resolve in the list query, not per delegate

`requestShotsFiltered` gains `LEFT JOIN recipes r ON r.id = shots.recipe_id`, selecting
`r.name`, `r.drink_type`, `r.archived` alongside `shots.recipe_id`.

*Why:* the tempting alternative is a `RecipeResolver` per delegate — the component
already exists and is correct on Shot Detail. In a list it is one async DB request per
visible row, re-fired on every scroll recycle. Naming it here so the next person does not
reach for it.

*Cost:* the join is on the `recipes` primary key against a table holding tens of rows,
inside a query that already runs off the main thread via `runDetachedDbThread`. There is
nothing to measure and nothing to cache; no comment claiming a measurement should be
written at the call site.

`requestAutoFavorites` / `requestAutoFavoriteGroupDetails` need only `recipe_id` (for the
button gate), not the join.

### Row layout: recipe takes the identity slot, profile demotes

```
recipe-driven                          recipe-less (identity line unchanged)
┌──────────────────────────────────┐   ┌──────────────────────────────────┐
│ Aug 1, 8:25 AM  ≋ Dad Monday     │   │ Aug 1, 8:25 AM   D-Flow / Q      │
│ D-Flow / Q · Sweet Bloom  8 · 800│   │ Sweet Bloom Hometown     8 · 800 │
│ 18.0g → 36.2g       31.4s        │   │ 18.0g → 36.2g       31.4s        │
└──────────────────────────────────┘   └──────────────────────────────────┘
                                            ↑ grind moved here from "(8)"
```

*Why the recipe outranks the profile:* for a user with custom names, the recipe is what
they call the drink; the profile is machinery. Nothing is lost — the profile stays on the
second line.

*Alternatives considered:* a separate fourth line for the recipe (taller rows, worst case
for default names); a leading chip on the second line (rejected by the maintainer — it
repeats badly when the default name is kept).

### Grind moves to the metrics line, labelled

The grind leaves the identity line entirely and joins the dose/yield and duration on the
metrics line, as `Grind 8.75 · 1500`. The duration is labelled `Time 19.4s` to match. The
dose/yield keeps no label — the `→` already says what it is, and "Dose 18.0g → 34.7g"
reads worse than the arrow alone.

*The problem being solved:* recipe-less rows used to render `bean (grind)` in a single
elided `Text`, so a long roaster name silently truncated the grind — the one varying
dial-in number the list is scanned for.

*Two layouts were tried and rejected before this one*, and the reasons are worth keeping
because both look correct on paper:

- **Grind pinned right on the secondary line**, identity cell `Layout.fillWidth`. The
  fillWidth cell grows into all the slack, so on a wide row the grind ends up hard
  against the right edge, far from the text it belongs to, reading as unrelated to the
  row.
- **The same, capped by `Layout.maximumWidth: implicitWidth`** so the cell can shrink and
  elide but not grow. This does keep the grind adjacent — but it is machinery in service
  of putting a labelled metric somewhere it does not belong. Once the value carries a
  label, the metrics line is simply where it goes, and the secondary line reverts to a
  plain full-width `Text`.

*Why a label is not optional:* on the metrics line the grind sits among other bare
numbers, and `8.75 · 1500` identifies nothing. The old `(8)` parenthetical was only
self-describing because it trailed the bean.

*Accepted cost:* every existing recipe-less row shifts visually — the grind leaves its
parenthetical on line 2 and appears as a labelled metric on line 3.

### Drink-type icon

`DrinkType.icon(t)` returns a themed SVG (`qrc:/icons/steam.svg` etc.), rendered through
`ColoredIcon` — not an emoji, so the macOS colour-glyph render-thread hazard does not
arise. `icon()` already falls through to `espresso.svg` for an unknown or empty type, so
a pre-migration-28 recipe with no stored `drink_type` gets a plausible icon rather than a
hole; no block-derived fallback (`DrinkType.fromRecipeMap`) is needed in the list.

This also gives the history row something it has never had: at-a-glance drink type.
`shots.beverage_type` is stored today and rendered nowhere.

### Archived recipes: dimmed plus announced

The name renders in the secondary text colour rather than the primary accent. Because
dimming is colour-only, the row's `Accessible.name` gains the archived state — the
"never the only carrier" rule. Archived recipes keep matching search: the shot happened.

### Search: three mechanisms, deliberately different scopes

| | Matches | Scope | Survives rename |
|---|---|---|---|
| bare text | notes, bean, profile, grinder, **recipe name** | broad | n/a |
| `recipe:` | recipe name only | narrow | n/a |
| tap-through | one recipe, by id | exact | yes |

Bare text pulls up more than expected by design — that is what a free-text box is for.
`recipe:` is the instrument for narrowing it.

**`recipe:` is the first string-valued keyword.** Every existing one is numeric
(`dose:16-18`) or boolean (`channeling:yes`), so no existing regex has had to decide
where a term ends. Two forms:

```
recipe:dad              → names containing "dad"          "Dad Monday" + "Dad Tuesday"
recipe:"dad tuesday"    → names containing "dad tuesday"   "Dad Tuesday"
```

Parse with `/\brecipe:(?:"([^"]*)"|(\S+))/i`, stripped from the search text like the other
keywords before the remainder goes to FTS. An unterminated quote runs to end-of-string
rather than failing.

*Why quoted stays a SUBSTRING and not an exact match:* a default-named recipe is
`Hometown Blend Latte · D-Flow / Q`; exact match would require typing that in full,
middot included. "Exactly this one recipe" is already served better by the tap-through,
which compares an id, not a string. A recipe name containing a literal `"` is
unmatchable by keyword — rare, and the tap-through covers it.

*SQL:* both forms and the bare-text case produce the same clause shape, built by
concatenation with `%`/`_`/`\`/`'` escaped and `ESCAPE '\'`, exactly as the grinder clause
is built (`QString::arg` is unusable there — the escaped value carries `%` that would
collide with `%N` placeholders):

```sql
recipe_id IN (SELECT id FROM recipes WHERE LOWER(name) LIKE '%<term>%' ESCAPE '\')
```

For bare text it is OR'd into the FTS/grinder disjunction; for `recipe:` and the
tap-through it is an AND term in `buildFilterQuery` (`recipeName` / `recipeId` on
`ShotFilter`).

### Tap-through and the existing `initialFilter` channel

The tap-through reuses the `initialFilter` mechanism AutoFavoritesPage already uses. Note
`ShotHistoryPage.qml:269` suppresses free-text FTS whenever `initialFilter` is set — a
recipe-id filter inherits that, which is correct here: the filter is exact and layering
fuzzy text on it would only subtract.

Saved searches store search text, so `recipe:"dad tuesday"` is savable and a tap-through
id filter is not. That asymmetry is intended and is a further reason both exist.

## Risks / Trade-offs

- **Every existing recipe-less row shifts visually** (grind moves right) → accepted
  explicitly by the maintainer; it also fixes the silent grind truncation those rows have
  today.
- **Default-named recipes stutter** — line 1 "Hometown Blend Latte · D-Flow / Q" over line
  2 "D-Flow / Q · Sweet Bloom Hometown" → accepted. The cure is renaming the recipe, which
  costs the user nothing. Building a name-shape heuristic to auto-collapse would be
  complexity with no measurable user-felt win.
- **Second line is now longer on recipe rows** and will elide on a phone → the grind is
  pinned, so what is lost is the profile/bean tail, which is the least surprising loss on
  a row whose recipe already implies both.
- **Recipe renamed to something misleading relabels history** → inherent to reference
  semantics, already the accepted rule on Shot Detail.
- **Four surfaces to keep in sync** (app row, Auto Favorites, web, MCP) → the promote-gate
  condition is one predicate (`recipeId > 0`) and belongs in the tasks list for each
  surface rather than being re-derived; the web and app rows are covered by the same spec
  requirement so a drift is a spec failure, not a style question.

## Migration Plan

None. No schema change, no data backfill, no FTS rebuild. `idx_shots_recipe_id` already
exists. The feature is inert for users with no recipes: `recipe_id` is NULL, the LEFT JOIN
yields nulls, and every row renders exactly as it does today apart from the pinned grind.

## Open Questions

None outstanding. Resolved during exploration:

- Snapshot vs reference → **reference**.
- Row layout → **recipe takes the identity slot, profile demotes** (layout A).
- Drink-type icon → **in scope**.
- Grind → **pinned right, one shape for all rows**.
- Archived recipe → **dimmed**, plus announced.
- `recipe:` keyword → **needed**, in single-token and quoted forms, both substring;
  bare free text also matches, and pulling up more than expected is the intended
  difference between the two.
- Promote button on recipe-driven shots → **hidden**, on all surfaces.
