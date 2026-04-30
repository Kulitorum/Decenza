# Change: Split `shothistorystorage.cpp` by concern

## Why

`src/history/shothistorystorage.cpp` is ~2500 lines and has grown five distinct logical concerns:

1. **Database lifecycle**: `initialize`, migrations, `withTempDb` integration.
2. **Save path**: `saveShot`, `saveShotData`, `saveShotStatic`, the helper `prepareAnalysisInputs`, all the data-extraction-on-main-thread logic.
3. **Load + recompute**: `loadShotRecordStatic`, the badge recompute block, the lazy-persist write-back, `requestReanalyzeBadges`, `computeDerivedCurves`, `computePhaseSummaries`.
4. **Serialization**: `convertShotRecord` (~180 lines including the new cached-analysis fast path), the `pointsToVariant` helpers.
5. **Query helpers**: `requestShotsFiltered`, `buildFilterQuery`, `requestRecentShotsByKbId`, `loadRecentShotsByKbIdStatic`, `requestAutoFavorites`, `requestDistinctCache`, the distinct-value cache logic, `queryGrinderContext`.

The file is well-organized internally but the size makes navigation hard, and the five concerns have weak inter-dependencies (concern 5 only needs the same DB connection; concerns 2–4 share `ShotRecord` shape but not implementation). A future split could move concerns 4 and 5 to separate translation units without breaking encapsulation.

This change splits concern 5 (query helpers) into a new `src/history/shothistorystorage_queries.cpp` and concern 4 (serialization) into a new `src/history/shothistorystorage_serialize.cpp`. Both stay in the `ShotHistoryStorage` class — the split is purely textual (multiple `.cpp` files contributing to the same class), no public API change.

## What Changes

- **ADD** `src/history/shothistorystorage_queries.cpp` containing the implementations of `requestShotsFiltered`, `buildFilterQuery`, `requestRecentShotsByKbId`, `loadRecentShotsByKbIdStatic`, `requestAutoFavorites`, `requestAutoFavoriteGroupDetails`, `requestDistinctCache`, `getDistinctBeanBrands` and friends, `queryGrinderContext`. ~700 lines moved out.
- **ADD** `src/history/shothistorystorage_serialize.cpp` containing `convertShotRecord` and its helper lambdas. ~200 lines moved out.
- **MODIFY** `src/history/shothistorystorage.cpp` keeps DB lifecycle, save path, load + recompute. ~1500 lines remaining.
- **MODIFY** `src/CMakeLists.txt` to compile all three TUs.
- **NO change** to `shothistorystorage.h` (declarations stay in one header), the `ShotHistoryStorage` class shape, or any caller.

## Impact

- Affected specs: none (organizational refactor only — no behavior change).
- Affected code: file-level reorganization in `src/history/`. `CMakeLists.txt` updated.
- User-visible behavior: identical.
- Risk: medium. The mass-move is bigger than D/E/F combined; line-count alone makes review harder. Mitigation: split into two PRs if reviewers want — one for the queries split, one for the serialization split.

## Why this is lower priority

The file-size pain is real but cosmetic. Nothing in the file is *wrong*, and the splits don't fix any drift hazard or duplication. This is purely a "future contributor friction" cleanup. I'd defer it unless you're already opening the file frequently and finding the navigation cost noticeable.

## Out of scope

- Splitting concerns 1–3 further. Those are tightly coupled (save and load share the badge cascade; the `withTempDb` helper threads through both). Leave them.
- Renaming the class or moving anything to a new namespace.
- Header reorganization. The single header `shothistorystorage.h` is fine.
