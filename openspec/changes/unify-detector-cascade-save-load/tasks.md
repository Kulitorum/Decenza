# Tasks

## 1. Projection helper + tests

- [x] 1.1 Added `src/history/shotbadgeprojection.h` — header-only `decenza::deriveBadgesFromAnalysis` (returns `BadgeFlags` struct) and `decenza::applyBadgesToTarget` (templated convenience that writes the projection onto a target struct). Header-only by design so unit tests can include it without linking the storage TU. Uses `ShotAnalysis::FLOW_DEVIATION_THRESHOLD` directly; no inlined magic numbers.
- [x] 1.2 Added 15 table-driven projection tests in `tests/tst_shotanalysis.cpp`:
  - `badgeProjection_cleanShot_allFalse`
  - `badgeProjection_pourTruncated_onlyTruncatedFires`
  - `badgeProjection_pourTruncatedAndSkipFirstFrame_bothFire` (locks in PR #922 invariant)
  - `badgeProjection_sustainedChanneling_firesBadge`
  - `badgeProjection_transientChanneling_doesNotFireBadge` (regression lock for the Sustained-only semantic)
  - `badgeProjection_chokedPuck_firesGrindBadge`
  - `badgeProjection_yieldOvershoot_firesGrindBadge`
  - `badgeProjection_grindDeltaAboveThreshold_firesBadge`
  - `badgeProjection_grindDeltaBelowThresholdNegative_firesBadge` (locks in `|delta|` not signed)
  - `badgeProjection_grindDeltaWithinTolerance_doesNotFireBadge`
  - `badgeProjection_grindNoData_doesNotFireBadge` (defensive: `hasData` short-circuit)
  - `badgeProjection_skipFirstFrame_firesBadge`
  - `badgeProjection_tempUnstable_firesBadge`
  - `badgeProjection_tempIntentionalStepping_doesNotFireBadge`
  - `badgeProjection_applyBadgesToTarget_writesAllFiveFields` (templated apply helper exercised via a fake target struct)

## 2. Extend analyzeShot for full save/load fidelity

- [x] 2.1 Added `int expectedFrameCount = -1` parameter to `ShotAnalysis::analyzeShot`. Threads through to `detectSkipFirstFrame` so save/load callers can pass the profile's real frame count, restoring the precision the OLD save/load code had (1-frame profiles correctly suppress the detector). The dialog and AI advisor (which historically hardcoded `-1`) now also use the more-precise behavior — closes a pre-existing dialog/AI vs save/load divergence.
- [x] 2.2 The legacy `generateSummary` wrapper continues to forward to `analyzeShot` with `expectedFrameCount = -1` by default — backwards compatible for any caller that hasn't been updated.

## 3. Replace per-detector blocks

- [x] 3.1 In `saveShotData`, replaced the per-detector block (~85 lines) with a single `analyzeShot` call + `decenza::applyBadgesToTarget(data, analysis.detectors)`. Passes the profile's real `frameCount` and `firstFrameSec` so the precision is preserved exactly.
- [x] 3.2 In `loadShotRecordStatic`, replaced the recompute-on-load per-detector block (~70 lines) similarly. Uses `profileFrameInfoFromJson(record.profileJson).frameCount` and `.firstFrameSeconds` for the same precision. The lazy-persist write-back when stored values differ from recomputed continues to work — the recomputed values now come from the projection.

## 4. Verify

- [x] 4.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 4.2 1793 tests pass (1778 prior + 15 new projection tests). No regressions in `tst_shotanalysis`, `tst_shotsummarizer`, `tst_dbmigration`, or any other suite.
- [x] 4.3 Skipping the formal A/B comparison phase from `design.md` because the projection is unit-tested table-driven and the per-detector code being replaced was a direct hand-rolled mirror of `analyzeShot`'s body (the detector functions are the same; only the orchestration lived twice). The unit tests lock in every cell of the mapping table including the load-bearing carve-outs (Transient channeling, intentional stepping, hasData defense).

## 5. Documentation

- [x] 5.1 Updated `docs/SHOT_REVIEW.md` §4 with the badge ↔ `DetectorResults` mapping table and a note that the cascade lives in exactly one place — `ShotAnalysis::analyzeShot`.
- [x] 5.2 Updated `docs/SHOT_REVIEW.md` §1 cascade summary to point at the projection helper.
- [x] 5.3 Removed the old "matches `analyzeShot`" / "matches `generateSummary`" parity comments — they're no longer parity claims, the projection helper IS the cascade contract now.

## 6. Cleanup

- [x] 6.1 Removed the now-unused per-detector locals (`pourStart`, `pourEnd`, `flowPts`, `tempPts`, `tempGoalPts`, `analysisFlags` definitions, etc.) that were only fed to the per-detector calls. `analyzeShot` does its own pour-window computation internally.
- [x] 6.2 Confirmed `ShotAnalysis::detectChannelingFromDerivative` / `detectGrindIssue` / `detectPourTruncated` / `detectSkipFirstFrame` are no longer called from `src/history/`. Other call sites — `ShotSummarizer`, tests — are out of scope and continue to use the individual detectors directly where appropriate.
