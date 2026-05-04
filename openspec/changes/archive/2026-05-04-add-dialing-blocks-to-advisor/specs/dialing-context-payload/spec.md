# dialing-context-payload — Delta

## ADDED Requirements

### Requirement: Dialing block builders SHALL be shared between MCP and in-app advisor surfaces

The block-construction code that produces `dialInSessions`, `bestRecentShot`, `sawPrediction`, and `grinderContext` for `dialing_get_context` SHALL be exported from a shared header (`src/mcp/mcptools_dialing_blocks.h`) so both `mcptools_dialing.cpp` and the in-app advisor's user-prompt enrichment path call the same code. Inline construction of these blocks inside `mcptools_dialing.cpp` SHALL be removed once the helpers are in place.

The shared module SHALL expose, at minimum:

- `QJsonArray buildDialInSessionsBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, int historyLimit)` — same `kDialInSessionGapSec` threshold, same `groupSessions` + `hoistSessionContext` composition, same per-shot serialization.
- `QJsonObject buildBestRecentShotBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, const ShotProjection& currentShot)` — same `kBestRecentShotWindowDays = 90` constant, same `enjoyment > 0` filter, same `changeFromBest` diff via `McpDialingHelpers::buildShotChangeDiff`.
- `QJsonObject buildGrinderContextBlock(QSqlDatabase& db, const QString& grinderModel, const QString& beverageType, const QString& beanBrand)` — same bean-scoped → cross-bean fallback semantics, same `allBeansSettings` tagging when bean-scoped is sparse.
- `QJsonObject buildSawPredictionBlock(Settings* settings, ProfileManager* profileManager, const ShotProjection& currentShot)` — main-thread only (touches `settings->calibration()` and `profileManager->baseProfileName()`); same espresso-only, scale + profile, and flow-data-present gates as today.

The response shape of `dialing_get_context` SHALL remain byte-equivalent after the refactor. Existing `tst_mcptools_dialing` tests SHALL pass without modification (the change is a pure refactor at the dialing tool's surface).

#### Scenario: dialing_get_context response is byte-equivalent before and after the refactor

- **GIVEN** a fixed DB state, a fixed resolved shot, and fixed Settings + ProfileManager state
- **WHEN** `dialing_get_context` is invoked before the refactor and after the refactor
- **THEN** the two response JSON strings SHALL be byte-for-byte identical

#### Scenario: Shared helpers are the single source of truth for the four blocks

- **GIVEN** any change to the shape, gating, or content of `dialInSessions`, `bestRecentShot`, `sawPrediction`, or `grinderContext`
- **WHEN** the change is made
- **THEN** it SHALL be made in `src/mcp/mcptools_dialing_blocks.h` (or its `.cpp`)
- **AND** both `dialing_get_context` and the in-app advisor SHALL pick up the change automatically because both call the helpers
