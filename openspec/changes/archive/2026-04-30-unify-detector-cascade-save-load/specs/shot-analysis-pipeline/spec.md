# shot-analysis-pipeline

## ADDED Requirements

### Requirement: Save and load paths SHALL derive boolean quality badges from `DetectorResults` via a single documented projection

`ShotHistoryStorage::saveShotData` (save-time badge computation) and `ShotHistoryStorage::loadShotRecordStatic` (recompute-on-load) SHALL invoke `ShotAnalysis::analyzeShot(...)` exactly once per shot and project the five boolean badge columns from the returned `DetectorResults` struct using the documented mapping. Neither function SHALL retain hand-rolled per-detector calls or hand-rolled cascade gate conditions; the cascade SHALL live in exactly one place — `ShotAnalysis::analyzeShot`.

The projection mapping SHALL be implemented in `decenza::deriveBadgesFromAnalysis` (header-only, in `src/history/shotbadgeprojection.h`) as:

| Badge column | `DetectorResults` projection |
|---|---|
| `pourTruncatedDetected` | `d.pourTruncated` |
| `channelingDetected` | `d.channelingSeverity == "sustained"` (Transient does NOT set the badge) |
| `temperatureUnstable` | `d.tempUnstable` |
| `grindIssueDetected` | `d.grindHasData && (d.grindChokedPuck || d.grindYieldOvershoot || std::abs(d.grindFlowDeltaMlPerSec) > FLOW_DEVIATION_THRESHOLD)` |
| `skipFirstFrameDetected` | `d.skipFirstFrame` |

`FLOW_DEVIATION_THRESHOLD` SHALL be read from `ShotAnalysis::FLOW_DEVIATION_THRESHOLD`; consumers MUST NOT inline the numeric value.

The lazy-persist write-back in `loadShotRecordStatic` (when stored badge columns differ from recomputed values, the recomputed values are written back to the DB) SHALL continue to function; the recomputed values just come from the projection helper instead of from per-detector calls.

`ShotAnalysis::analyzeShot` SHALL accept an optional `expectedFrameCount` parameter and forward it to `detectSkipFirstFrame`. Save and load paths SHALL pass the profile's actual frame count so 1-frame profiles correctly suppress the skip-first-frame detector. Backwards-compatible: the parameter defaults to `-1` (unknown), preserving behavior for callers (legacy `generateSummary` wrapper) that don't pass it.

The DB schema, the badge column types, and the badge-driven UI surfaces (history-list filter chips, badge UI, `shots_list` MCP) are OUT OF SCOPE — they continue to read the same five boolean columns; only the *production* of those values is unified.

The Sustained-only semantic for `channelingDetected` SHALL be preserved exactly. A shot whose `DetectorResults.channelingSeverity` is `"transient"` MUST result in `channelingDetected = false`. This matches the badge behavior established by PR #922 and is documented in the projection table.

#### Scenario: Clean shot projects to all-false badge columns

- **GIVEN** a shot whose `analyzeShot` returns `DetectorResults` with `verdictCategory = "clean"` and all detector gates clear
- **WHEN** save-time or load-time badge derivation runs
- **THEN** all five boolean badge columns SHALL be `false`

#### Scenario: Pour-truncated cascade dominates the projection

- **GIVEN** a shot whose `analyzeShot` returns `pourTruncated = true` (and consequently `channelingChecked = false`, `grindChecked = false`, `tempUnstable = false`)
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `pourTruncatedDetected` SHALL be `true`
- **AND** `channelingDetected`, `temperatureUnstable`, `grindIssueDetected` SHALL each be `false`
- **AND** `skipFirstFrameDetected` SHALL reflect `d.skipFirstFrame` independently (skip-first-frame is NOT suppressed by the cascade, matching PR #922's invariant)

#### Scenario: Transient channeling does NOT set the badge

- **GIVEN** a shot whose `analyzeShot` returns `channelingSeverity = "transient"`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `channelingDetected` SHALL be `false`
- **AND** the Shot Summary dialog SHALL still render the "Transient channel at Xs (self-healed)" caution line (the dialog reads `summaryLines`, not the boolean badge)

#### Scenario: Sustained channeling sets the badge

- **GIVEN** a shot whose `analyzeShot` returns `channelingSeverity = "sustained"`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `channelingDetected` SHALL be `true`

#### Scenario: Choked-puck shot fires the grind badge via the chokedPuck arm

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, `grindChokedPuck = true`, `grindFlowDeltaMlPerSec` near zero
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `true`

#### Scenario: Yield-overshoot shot fires the grind badge via the yieldOvershoot arm

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, `grindYieldOvershoot = true`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `true`

#### Scenario: Flow delta within tolerance does NOT fire the grind badge

- **GIVEN** a shot whose `analyzeShot` returns `grindHasData = true`, both `grindChokedPuck` and `grindYieldOvershoot` false, and `|grindFlowDeltaMlPerSec| <= FLOW_DEVIATION_THRESHOLD`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `grindIssueDetected` SHALL be `false`

#### Scenario: Intentional temperature stepping does NOT fire the temp badge

- **GIVEN** a shot using a profile whose temperature goal range exceeds `TEMP_STEPPING_RANGE` (e.g. D-Flow 84→94°C)
- **AND** `analyzeShot` returns `tempIntentionalStepping = true`, `tempUnstable = false`
- **WHEN** save-time or load-time badge derivation runs
- **THEN** `temperatureUnstable` SHALL be `false`

#### Scenario: 1-frame profile suppresses skip-first-frame detection

- **GIVEN** a shot from a profile with `frameCount = 1`
- **WHEN** save or load runs `analyzeShot` with `expectedFrameCount = 1`
- **THEN** `analyzeShot` SHALL pass `expectedFrameCount` through to `detectSkipFirstFrame`
- **AND** `detectSkipFirstFrame` SHALL return `false` (no second frame to skip to)
- **AND** `skipFirstFrameDetected` SHALL be `false`
