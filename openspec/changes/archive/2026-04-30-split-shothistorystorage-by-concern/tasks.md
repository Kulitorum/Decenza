# Tasks

## 1. Queries split (~700 lines)

- [x] 1.1 Created `src/history/shothistorystorage_queries.cpp` with a subset `#include` block (Qt SQL/json/locale + `core/dbutils.h` + `core/grinderaliases.h`) plus a `using decenza::storage::detail::use12h;` for the shared helper. Header preamble enumerates the five owning concerns (filtered queries, recents-by-kbId, distinct cache, auto-favorites, grinder context).
- [x] 1.2 Moved `requestShotsFiltered`, `buildFilterQuery`, `parseFilterMap`, `formatFtsQuery`, and the `s_sortColumnMap` whitelist into the new file. The QSqlQuery binding lambda inside `requestShotsFiltered` moves with it.
- [x] 1.3 Moved `requestRecentShotsByKbId` and `loadRecentShotsByKbIdStatic`.
- [x] 1.4 Moved `requestAutoFavorites` and `requestAutoFavoriteGroupDetails`. The auto-favorite SQL templates and the `use12h()` call inside the latter move along with them.
- [x] 1.5 Moved `requestDistinctCache`, `requestDistinctValueAsync`, `getDistinctValues`, `invalidateDistinctCache`, the `s_allowedColumns` whitelist, and the `getDistinct{BeanBrands,BeanTypes,Grinders,GrinderSettings,Baristas,BeanTypesForBrand,GrinderBrands,GrinderModelsForBrand,GrinderBurrsForModel,GrinderSettingsForGrinder}` family. `sortGrinderSettings` moves with them since it's only called from `requestDistinctValueAsync`.
- [x] 1.6 Moved `queryGrinderContext` and `requestUpdateGrinderFields`.

## 2. Serialization split (~200 lines)

- [x] 2.1 Created `src/history/shothistorystorage_serialize.cpp`. Also created `src/history/shothistorystorage_internal.{h,cpp}` to host the file-static helpers (`use12h`, `prepareAnalysisInputs`, `profileFrameInfoFromJson`, plus the `ProfileFrameInfo` and `AnalysisInputs` structs) — they're shared between the main TU and the new serialize TU (and will be needed by the queries TU when 1.x lands), so an internal-only header keeps the public `shothistorystorage.h` unchanged. All five live under `decenza::storage::detail`.
- [x] 2.2 Moved `ShotHistoryStorage::convertShotRecord` (the cached-vs-fallback `analyzeShot` branch + `prepareAnalysisInputs` call + the `pointsToVariant` lambda) into the serialize TU. Main TU now carries a `// convertShotRecord — moved to shothistorystorage_serialize.cpp.` placeholder comment at the original location.

## 3. CMake

- [x] 3.1 Updated `CMakeLists.txt` (root): all three new TUs (`shothistorystorage_internal.cpp`, `shothistorystorage_serialize.cpp`, `shothistorystorage_queries.cpp`) are listed alongside `shothistorystorage.cpp`. `shothistorystorage_internal.h` added to the header list (parallel to `shothistorystorage.h`). Header for the queries TU is intentionally omitted — its functions are member definitions on `ShotHistoryStorage` and need no separate header.
- [x] 3.2 Updated `tests/CMakeLists.txt`: `HISTORY_SOURCES` and the two MCP test targets that pull in `shothistorystorage.cpp` directly (`tst_mcptools_write`, `tst_mcpserver_session`) all picked up the queries TU via the same `replace_all` mechanism that added the serialize TU in part 1.

## 4. Verify

- [x] 4.1 Build clean via Qt Creator MCP — succeeded with 0 errors. The 2 warnings reported (`linking with dylib '...openssl@3...' which was built for newer version 26.0`) are pre-existing OpenSSL-version mismatches in the dev environment, not introduced by this PR.
- [x] 4.2 All 1811 tests pass; 0 failed, 0 with warnings.
- [x] 4.3 Re-grepped for `convertShotRecord` / `prepareAnalysisInputs` / `profileFrameInfoFromJson` / `use12h` — each function has exactly one definition; matches in other files are declarations, comments, or `using` aliases.
- [x] 4.4 Line counts post-split: `shothistorystorage.cpp` = 2584 (down from 3925), `shothistorystorage_queries.cpp` = 1139, `shothistorystorage_serialize.cpp` = 215, `shothistorystorage_internal.cpp` = 47, `shothistorystorage_internal.h` = 52. Combined: the four TUs sum to 4037 lines (33% larger than the original; growth comes from the per-TU includes/preambles), but no single file is now larger than 2584. Main TU shed ~1300 lines across the two splits.

## 5. Optional

- [x] 5.1 Shipped as two PRs per the proposal's recommendation: PR 1 (`#951`) carried the serialization split + shared internal helpers; PR 2 (this one) carries the queries split. The queries body was independent of the serialize move so the two PRs landed in either order.
