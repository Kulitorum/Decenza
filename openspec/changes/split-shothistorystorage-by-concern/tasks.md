# Tasks

## 1. Queries split (~700 lines)

- [ ] 1.1 Create `src/history/shothistorystorage_queries.cpp`. Add the same `#include` block the main TU uses (subset for query needs).
- [ ] 1.2 Move `requestShotsFiltered`, the `buildFilterQuery` static helper, the QSqlQuery binding code into the new file.
- [ ] 1.3 Move `requestRecentShotsByKbId`, `loadRecentShotsByKbIdStatic`.
- [ ] 1.4 Move `requestAutoFavorites`, `requestAutoFavoriteGroupDetails`, the auto-favorite SQL templates.
- [ ] 1.5 Move `requestDistinctCache`, the distinct-value cache mutation, `getDistinctBeanBrands` and friends, `getDistinctBeanTypesForBrand`, `getDistinctGrinderBrands`, etc.
- [ ] 1.6 Move `queryGrinderContext`, `requestUpdateGrinderFields`.

## 2. Serialization split (~200 lines)

- [x] 2.1 Created `src/history/shothistorystorage_serialize.cpp`. Also created `src/history/shothistorystorage_internal.{h,cpp}` to host the file-static helpers (`use12h`, `prepareAnalysisInputs`, `profileFrameInfoFromJson`, plus the `ProfileFrameInfo` and `AnalysisInputs` structs) — they're shared between the main TU and the new serialize TU (and will be needed by the queries TU when 1.x lands), so an internal-only header keeps the public `shothistorystorage.h` unchanged. All five live under `decenza::storage::detail`.
- [x] 2.2 Moved `ShotHistoryStorage::convertShotRecord` (the cached-vs-fallback `analyzeShot` branch + `prepareAnalysisInputs` call + the `pointsToVariant` lambda) into the serialize TU. Main TU now carries a `// convertShotRecord — moved to shothistorystorage_serialize.cpp.` placeholder comment at the original location.

## 3. CMake

- [x] 3.1 Updated `CMakeLists.txt` (root): `src/history/shothistorystorage_internal.cpp` and `src/history/shothistorystorage_serialize.cpp` added alongside `shothistorystorage.cpp`. `shothistorystorage_internal.h` added to the header list (parallel to `shothistorystorage.h`).
- [x] 3.2 Updated `tests/CMakeLists.txt`: `HISTORY_SOURCES` and the two MCP test targets that pull in `shothistorystorage.cpp` directly (`tst_mcptools_write` at line 478, `tst_mcpserver_session` at line 502) all picked up the two new TUs via a `replace_all` edit on the source line.

## 4. Verify

- [x] 4.1 Build clean via Qt Creator MCP — succeeded with 0 errors. The 2 warnings reported (`linking with dylib '...openssl@3...' which was built for newer version 26.0`) are pre-existing OpenSSL-version mismatches in the dev environment, not introduced by this PR.
- [x] 4.2 All 1811 tests pass; 0 failed, 0 with warnings.
- [x] 4.3 Re-grepped for `convertShotRecord` / `prepareAnalysisInputs` / `profileFrameInfoFromJson` / `use12h` — each function has exactly one definition; matches in other files are declarations, comments, or `using` aliases.
- [x] 4.4 Line counts post-split: `shothistorystorage.cpp` = 3679 (down from 3925), `shothistorystorage_serialize.cpp` = 214, `shothistorystorage_internal.cpp` = 47, `shothistorystorage_internal.h` = 51. The serialize split shed ~250 lines from the main TU; the queries split (1.x, deferred to a follow-up) will trim it further.

## 5. Optional

- [x] 5.1 Per the proposal's recommendation, this PR ships ONLY the serialization split + the shared-internal-helpers extraction. The queries split (tasks 1.1–1.6) is deferred to a follow-up PR — easier to review, smaller diff, and the queries body is independent of the serialize move so the order doesn't matter.
