# shot-analysis-pipeline

## ADDED Requirements

### Requirement: ShotHistoryStorage's implementation SHALL be split across multiple translation units by concern

`ShotHistoryStorage`'s ~2500-line implementation SHALL be split across at least three translation units to make navigation tractable for future contributors:

1. `shothistorystorage.cpp` — DB lifecycle, save path, load + recompute. Core lifecycle for a shot record.
2. `shothistorystorage_queries.cpp` — `requestShotsFiltered`, `requestRecentShotsByKbId`, `requestAutoFavorites`, distinct-value cache, `queryGrinderContext`. Read-only query helpers.
3. `shothistorystorage_serialize.cpp` — `convertShotRecord` and its `pointsToVariant` lambdas. Serialization to QVariantMap for QML / MCP / web consumption.

The class declaration SHALL remain in a single header (`shothistorystorage.h`); only the implementation is split. No public API change. The split SHALL preserve behavior — `git log --follow` continues to work for individual function histories because functions move atomically with their callers.

#### Scenario: Splitting does not change observable behavior

- **GIVEN** the codebase before the split, with all tests passing
- **WHEN** the split is applied
- **THEN** every test in `tst_dbmigration`, `tst_shotanalysis`, `tst_shotrecord_cache`, `tst_shotsummarizer`, and other suites that exercise `ShotHistoryStorage` SHALL continue to pass without modification

#### Scenario: Header surface is unchanged

- **GIVEN** the post-split codebase
- **WHEN** an external caller `#include "history/shothistorystorage.h"`
- **THEN** the same set of public methods SHALL be visible, with the same signatures, as before the split
