# Tasks

## 1. Queries split (~700 lines)

- [ ] 1.1 Create `src/history/shothistorystorage_queries.cpp`. Add the same `#include` block the main TU uses (subset for query needs).
- [ ] 1.2 Move `requestShotsFiltered`, the `buildFilterQuery` static helper, the QSqlQuery binding code into the new file.
- [ ] 1.3 Move `requestRecentShotsByKbId`, `loadRecentShotsByKbIdStatic`.
- [ ] 1.4 Move `requestAutoFavorites`, `requestAutoFavoriteGroupDetails`, the auto-favorite SQL templates.
- [ ] 1.5 Move `requestDistinctCache`, the distinct-value cache mutation, `getDistinctBeanBrands` and friends, `getDistinctBeanTypesForBrand`, `getDistinctGrinderBrands`, etc.
- [ ] 1.6 Move `queryGrinderContext`, `requestUpdateGrinderFields`.

## 2. Serialization split (~200 lines)

- [ ] 2.1 Create `src/history/shothistorystorage_serialize.cpp`.
- [ ] 2.2 Move `ShotHistoryStorage::convertShotRecord` (post-D this includes the cached-vs-fallback branch + `prepareAnalysisInputs` call). The helper lambdas it uses (`pointsToVariant`) move with it.

## 3. CMake

- [ ] 3.1 Update `src/CMakeLists.txt` to compile all three TUs alongside the existing `shothistorystorage.cpp`.
- [ ] 3.2 Update `tests/CMakeLists.txt` `HISTORY_SOURCES` set to include the new TUs (otherwise `tst_dbmigration`, `tst_shotrecord_cache`, etc. will fail to link).

## 4. Verify

- [ ] 4.1 Build clean (Qt Creator MCP).
- [ ] 4.2 All existing tests pass.
- [ ] 4.3 Re-grep for any function that ended up in two files (mass-move bugs).
- [ ] 4.4 Spot-check the resulting line counts: shothistorystorage.cpp should be ~1500 lines, queries ~700, serialize ~200.

## 5. Optional

- [ ] 5.1 If reviewers find the mass-move PR hard to review, split into two PRs (one for queries, one for serialization). Each is independent.
