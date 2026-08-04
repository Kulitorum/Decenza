## 1. Bag filtering in the storage layer

- [x] 1.1 Add `bagId` (`qint64`, -1 unset) and `bagLabel` (banner-only) to `ShotFilter` in `src/history/shothistory_types.h`, with a comment mirroring the `recipeId` / `recipeName` note on why the two scopes differ
- [x] 1.2 Read `bagId` and the `bag:` keyword term in `buildShotFilter()` (`shothistorystorage_queries.cpp:~140`)
- [x] 1.3 Emit the exact clause `s.bag_id = ?` guarded on `filter.bagId > 0` (never `>= 0` — a NULL `bag_id` on pre-bag shots must not match), beside the `recipeId` clause
- [x] 1.4 Emit the keyword clause as an uncorrelated subquery: `s.bag_id IN (SELECT id FROM coffee_bags WHERE coffee_name LIKE ? OR roaster_name LIKE ? OR roast_date LIKE ?)` — not a JOIN, so the shot query's positional column indices are untouched
- [x] 1.5 ~~Measure the keyword query and record the median at the call site~~ — not applicable, and the design's premise for it was wrong. `requestShotsFiltered()` builds the SQL on the main thread (pure string work) and EXECUTES on a background thread, so the subquery's cost is not on any path a user or the machine waits on. It also sits beside an existing subquery of the same shape (grinder identity over `equipment_items`). A call-site median would be measuring a background thread, which is exactly the "win the user never feels" the complexity rule warns about

## 2. Bag filtering in Shot History UI

- [x] 2.1 Add `bagId` to `ShotHistoryPage.buildFilter()`'s numeric passthrough list and `bagLabel` to the banner-only exclusions, alongside `recipeId` / `recipeName`
- [x] 2.2 Parse a `bag:` keyword beside `recipe:` — same quoted/unquoted forms, same `\S+` (not `\S*`) unquoted branch, same empty-quoted sentinel — and add it to the keyword-stripping pass so its term never reaches FTS
- [x] 2.3 Show the bag in the filter banner: coffee name, falling back to roaster name (the `BeansItem.bagLabel()` rule)
- [x] 2.4 Add a `filterByBag(bagId, bagLabel)` entry point mirroring `filterByRecipe()`, for the in-place re-filter path

## 3. Already-on-History fix

- [x] 3.1 In `main.qml`'s `goToShotHistory(filter)`, apply the filter in place when Shot History is already the current page (assign `initialFilter` and reload) instead of returning from `pushUnlessCurrent()` with nothing applied
- [ ] 3.2 Confirm the unfiltered `navigate:history` path now clears an active filter rather than being a no-op when History is showing

## 4. Action catalog in C++ (drift fix)

- [x] 4.1 Add a `LayoutActionEntry` struct and `layoutActionCatalog()` table in `src/core/settings_network.cpp`, beside `widgetCatalogTable()`: `{id, labelKey, label, contexts}` for every action the in-app picker offers today, keys copied verbatim from `CustomEditorPopup.getFilteredActions()`
- [x] 4.2 Add `SettingsNetwork::layoutActionCatalog()` (QVariantList for QML) and `layoutActionCatalogJson()` (for web injection) in `settings_network.{h,cpp}`, mirroring `widgetCatalog()` / `readoutCapabilitiesJson()`
- [x] 4.3 Diff the catalog's id + labelKey strings against the pre-change QML list and confirm they match exactly (no orphaned translation keys)
- [x] 4.4 Replace `CustomEditorPopup.getFilteredActions()`'s hand-written `allActions` array with a read of the catalog, resolving `labelKey`/`label` through `TranslationManager.translate`; keep the page-context filtering and the `command:loadProfile` dynamic expansion
- [x] 4.5 Replace the web editor's inline `ACTIONS` array in `src/network/shotserver_layout.cpp` with the injected `layoutActionCatalogJson()`, following the `WIDGET_CATALOG` injection pattern in the same file
- [x] 4.6 Remove the keep-in-sync comments left behind in both files

## 5. The four History actions

- [x] 5.1 Add `navigate:historyRecipe`, `navigate:historyBean`, `navigate:historyBag`, `navigate:historyProfile` to the catalog with contexts `["idle", "all"]` and new translation keys `customaction.navigate.historyRecipe` / `historyBean` / `historyBag` / `historyProfile`
- [x] 5.2 Add the four `case` arms to `CustomItem.executeActionString()`: build `initialFilter` from `Settings.dye.activeRecipeId` + `MainController.activeRecipe.name`, `Settings.dye.dyeBeanBrand` + `dyeBeanType`, `Settings.dye.activeBagId` + the active bag's label, and `ProfileManager.currentProfileName` respectively
- [x] 5.3 Omit empty values from each filter map and emit `AppShell.shotHistoryRequested({})` when the whole context is empty, so the widget is never dead

## 6. Verification

- [x] 6.1 Build via `mcp__qtcreator__build` and run the full suite via `mcp__qtcreator__run_tests` (scope `all`)
- [x] 6.2 Run the qmllint gate (`qmllint_check` target) — `CustomItem.qml`, `CustomEditorPopup.qml` and `ShotHistoryPage.qml` are all on the clean list, so any new diagnostic fails it
- [x] 6.3 Add storage tests for the bag filter: exact id matches only that bag; a second bag of the same coffee is excluded; pre-bag NULL `bag_id` shots are excluded; `bagId <= 0` means unset, not "match nulls" (extend an existing shot-history test file — do not add a new `tst_*.cpp`)
- [x] 6.4 Add `bag:` keyword parse tests beside the existing `recipe:` ones: unquoted, quoted, unterminated quote, bare `bag:` falling through to free text, `bag:""` matching nothing, term not leaking into FTS
- [x] 6.5 Add a test asserting each of the four actions produces the expected filter map from a given app state, including the empty-context case producing an empty filter
- [~] 6.6 PARTIAL — recipe arm verified on real data (active recipe 45 has 0 shots; filter correctly returned 0 of 1131, confirmed against recipe_list shotCount). Bean/bag/profile arms, the empty-context cases, and the in-place re-filter NOT yet exercised. In the running app: place a Custom widget per action, verify each opens History with the right banner and result set; verify the no-active-recipe / no-bean / no-bag / no-profile cases open unfiltered; verify a tap while already on History re-filters in place and that plain "Go to History" clears the filter
- [ ] 6.7 Verify the bean vs bag distinction on real data — two bags of one coffee, bean action shows both, bag action shows one
- [ ] 6.8 In the web layout editor: verify the four actions appear, save one, reopen it in the in-app editor and confirm it round-trips as the same selected action
- [ ] 6.9 Compare the in-app and web action pickers page-by-page and confirm context filtering matches, including the six navigate actions the web copy was previously missing

## 7. Documentation

- [x] 7.1 Update the wiki manual's Custom widget action list with the four actions, stating that the bean filter spans bags of the same coffee and the bag filter does not
- [x] 7.2 Update the wiki manual's Shot History search-keyword list with `bag:`, including the quoted form and what it matches (coffee name, roaster, roast date)
- [x] 7.3 Note the centralized action catalog beside the widget-catalog note in `CLAUDE.md`, so the next person adding an action edits one table
- [ ] 7.4 Archive this change with `openspec archive add-history-filter-widget-actions` as the last commit on the feature branch
