## 1. Query layer — resolve recipe identity

- [x] 1.1 Add `recipeId` (qint64, -1 = unset) and `recipeName` (QString) to `ShotFilter` in `src/history/shothistory_types.h`
- [x] 1.2 In `requestShotsFiltered` (`shothistorystorage_queries.cpp`), add `LEFT JOIN recipes r ON r.id = shots.recipe_id` to both the FTS and non-FTS SELECT branches, selecting `shots.recipe_id`, `r.name`, `r.drink_type`, `r.archived`; qualify existing bare column names as needed so the join does not make any reference ambiguous
- [x] 1.3 Map the four new columns into the result map as `recipeId`, `recipeName`, `recipeDrinkType`, `recipeArchived`
- [x] 1.4 Add the recipe-name subquery to the free-text disjunction, built by concatenation with `%`/`_`/`\`/`'` escaped and `ESCAPE '\'` — mirror the existing grinder clause exactly (never `QString::arg`); apply it to the count query as well as the data query
- [x] 1.5 Handle `filter.recipeName` in `buildFilterQuery` as an AND term using the same escaped-LIKE subquery
- [x] 1.6 Handle `filter.recipeId` in `buildFilterQuery` as `recipe_id = ?` (bound, not concatenated)
- [x] 1.7 Extend `parseFilterMap` to read `recipeId` and `recipeName` from the QVariantMap
- [x] 1.8 Add `recipe_id` to the SELECT in `requestAutoFavorites`, exposed as `recipeId` in its result map (no join needed — the gate only reads the id). `requestAutoFavoriteGroupDetails` is NOT in scope after all: it returns group stats and notes, not per-shot rows, so it carries no promote button to gate

## 2. Shot History row

- [x] 2.1 Identity line: when `model.recipeId > 0`, render `ColoredIcon` from `DrinkType.icon(model.recipeDrinkType)` followed by `model.recipeName`; otherwise render the profile name as today
- [x] 2.2 Dim the recipe name (secondary text colour rather than the primary accent) when `model.recipeArchived`
- [x] 2.3 Rebuild the secondary line as a `RowLayout`: identity cell (`Layout.fillWidth`, `elide: Text.ElideRight`) holding `profile · bean` on recipe rows and `bean` alone otherwise, plus a trailing grind cell at intrinsic width with no elide, `visible` only when a grind is recorded; keep the existing `"Grind: "` prefix fallback and the ` · rpm` pairing
- [x] 2.4 Update the row's `Accessible.name`: recipe name first when present, and the archived state included as text so the dimming is not colour-only
- [x] 2.5 Make the recipe name tappable — emit a recipe-id filter through the existing `initialFilter` channel; ensure the active-filter banner shows the recipe name and its Clear control works
- [x] 2.6 Hide the promote-to-recipe button when `model.recipeId > 0`
- [x] 2.7 Verify the row's `Accessible.role`/`focusable`/`onPressAction` and the new icon's `Accessible.ignored` per `docs/CLAUDE_MD/ACCESSIBILITY.md`; fix any pre-existing violations in the delegate while in the file

## 3. Search syntax

- [x] 3.1 Parse `recipe:` in `buildFilter()` with `/\brecipe:(?:"([^"]*)"|(\S+))/i`, setting `filter.recipeName`; treat an unterminated quote as running to end-of-string
- [x] 3.2 Strip the matched keyword from the text passed on as FTS `searchText`, alongside the existing numeric/boolean keyword stripping
- [ ] 3.3 Confirm `recipe:` composes with the numeric and boolean keywords in any order, and with trailing free text
- [x] 3.4 Add a `recipe:` row to the Keywords help sheet (label, description, example), internationalized via `TranslationManager.translate` with new keys

## 4. Other surfaces

- [x] 4.1 `qml/pages/AutoFavoritesPage.qml`: hide the promote-to-recipe button when the row's `recipeId > 0`
- [x] 4.2 Add the same `LEFT JOIN recipes` to the web shot-list query (`queryShotList` in `shotserver.cpp`, not `shotserver_shots.cpp`). NO server-side search clause: the web `/shots` search runs CLIENT-side in JS over already-rendered cards (`card.textContent`), so bare free text matches the recipe name for free once it is in the card, and `recipe:` is a JS keyword filtering on a new `data-recipe` attribute
- [x] 4.3 Web shot card: show recipe name + drink-type icon, dim when archived, and suppress the promote button when the shot has a recipe — reuse the shared page style/shell helpers, do not re-inline
- [x] 4.4 `src/mcp/mcptools_shots.cpp`: expose `recipeId` and `recipeName` on `shots_list` rows, omitting both when the shot used no recipe

## 5. Tests

- [x] 5.1 Added slots to the existing `tests/tst_dbmigration.cpp` class — `tst_shothistorystorage.cpp` does not exist, and tst_dbmigration already owned the grinder-pointer free-text search test, the exact precedent for this work. Helpers live under `private:`, NOT `private slots:` (Qt Test runs every private slot as a test)
- [x] 5.2 Test bare free text matching a recipe name that appears in no other field, and that the count query agrees with the data query
- [x] 5.3 Test `recipe:` single-token and quoted forms, including the quoted form disambiguating two names sharing a leading word, and that it does NOT match a bean of the same text
- [x] 5.4 Test `filter.recipeId` returns exactly that recipe's shots, including when a second recipe shares its name
- [x] 5.5 Test a name containing `%`, `_` and `'` is matched literally (escaping is not bypassed)
- [x] 5.6 Break each new query clause and confirm the corresponding test goes red before keeping it

## 6. Verification and docs

- [x] 6.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before building, per the shared-Qt-Creator rule
- [ ] 6.2 Open Shot History in the running app and check both row types, the archived dim, elide behaviour at a narrow width, and the tap-through — QML is verified manually; the test suite cannot see any of section 2
- [ ] 6.3 Read the `text-invariants.yml` PR run before merging (it gates `src/**`; nothing blocks a merge on it)
- [x] 6.4 Update `docs/CLAUDE_MD/RECIPES.md`: recipe identity now appears on the history row and in search; note the reference-not-snapshot rule extends beyond `shot-recipe-card`
- [ ] 6.5 Update the wiki manual (`Kulitorum/Decenza.wiki`) Shot History page: the recipe line, the drink-type icon, and the `recipe:` keyword — hold the push per the release-timing convention unless told otherwise
- [ ] 6.6 Open the PR, then run the automated `/pr-review-toolkit:review-pr` before merging
- [ ] 6.7 Archive the change + spec sync as the final commit on the same PR
