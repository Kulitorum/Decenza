# Change: Unify save-time and load-time detector cascades on `analyzeShot`

## Why

The shot-quality cascade (pourTruncated → channeling/temp/grind forced false) currently has **three** implementations:

1. `ShotHistoryStorage::saveShotData` — calls each detector individually with hand-rolled gate conditions and writes the boolean badge columns.
2. `ShotHistoryStorage::loadShotRecordStatic` — recomputes the same booleans on load using the same hand-rolled gates, persisting any drift back to the DB.
3. `ShotAnalysis::analyzeShot` — the structured pipeline introduced in PR #933, which runs the same detectors with the same gates and produces both prose lines and the `DetectorResults` struct.

PR #922 papered over the divergence between (1) and (2) by triplicating the gate logic. The comment-analyzer in PR #933's review surfaced that "matches `analyzeShot`" parity claims in (1) and (2) are what hold the cascade together — anyone adding a sixth detector or tweaking a gate has to update three places exactly the same way, or drift back into the inconsistencies #922 fixed.

This change collapses save and load onto the `analyzeShot` pipeline: they call it once and derive the boolean DB columns from `DetectorResults` via a small projection helper. After this change, the cascade lives in exactly one place — `analyzeShot` — and the badge columns become a deterministic projection of the typed struct.

## What Changes

- **ADD** `src/history/shotbadgeprojection.h` — header-only `decenza::deriveBadgesFromAnalysis` (returns `BadgeFlags`) and `decenza::applyBadgesToTarget` (applies the projection to a target struct). Pure functions, no side effects.
- **MODIFY** `src/history/shothistorystorage.cpp` `saveShotData` — replace the per-detector block (~85 lines) with a single `ShotAnalysis::analyzeShot(...)` call + `decenza::applyBadgesToTarget(data, analysis.detectors)`.
- **MODIFY** `src/history/shothistorystorage.cpp` `loadShotRecordStatic` — replace the recompute-on-load per-detector block (~70 lines) with the same single call + projection.
- **EXTEND** `ShotAnalysis::analyzeShot` to accept an `int expectedFrameCount = -1` parameter so the save/load paths can preserve their existing precision on `detectSkipFirstFrame` (passes the real frame count from the profile). The dialog/AI advisor previously hardcoded `-1` here, so this also closes a pre-existing precision gap between save/load and the dialog.
- **DOCUMENT** the badge ↔ `DetectorResults` mapping in `docs/SHOT_REVIEW.md` §4. Update §1 cascade summary to point at the projection.

## Mapping table

| Badge column | `DetectorResults` projection |
|---|---|
| `pourTruncatedDetected` | `d.pourTruncated` |
| `channelingDetected` | `d.channelingSeverity == "sustained"` (Transient does NOT fire the badge) |
| `temperatureUnstable` | `d.tempUnstable` |
| `grindIssueDetected` | `d.grindHasData && (d.grindChokedPuck \|\| d.grindYieldOvershoot \|\| std::abs(d.grindFlowDeltaMlPerSec) > FLOW_DEVIATION_THRESHOLD)` |
| `skipFirstFrameDetected` | `d.skipFirstFrame` |

## Out of scope

- The five legacy badge boolean columns stay in the schema. No migration drops them — `shots_list` MCP, history-list filter chips, and badge UI all read those columns directly.
- The `Transient` channeling state still does NOT set `channelingDetected = true` — only `Sustained` does. Deliberate semantic carry-over from PR #922.
- The bulk resweep tracked in #894 is unaffected; this change makes the recomputed values come from the same projection on every load, so resweep behavior is identical.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: save and load SHALL derive boolean badges from `DetectorResults` via the documented projection).
- Affected code: `src/history/shotbadgeprojection.h` (new), `src/history/shothistorystorage.cpp` (save block, load block), `src/ai/shotanalysis.{h,cpp}` (`expectedFrameCount` parameter), `docs/SHOT_REVIEW.md` §1 + §4, `tests/tst_shotanalysis.cpp` (new projection tests).
- User-visible behavior: identical badge state on every shot, by construction. The dialog and AI advisor get a small precision improvement on `skipFirstFrameDetected` for 1-frame profiles (previously a false-positive surface, now matches save/load's accuracy).
- Performance: save-time goes from N detector calls to 1 `analyzeShot` call. Load-time same. Both already linear in shot length; constant-factor reduction.
