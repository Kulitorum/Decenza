# Design: Unify save/load detector cascades on `analyzeShot`

## Why a design doc

This is the only one of the three parity follow-ups (A/B/C) that touches DB-write paths and visible badge state. The original plan called for a staged rollout with an A/B comparison phase before the old code was deleted. That phase was ultimately skipped — see "Why no A/B comparison phase" below.

## Mapping table — the contract

```cpp
flags.pourTruncatedDetected = d.pourTruncated;
flags.skipFirstFrameDetected = d.skipFirstFrame;
flags.channelingDetected     = (d.channelingSeverity == QStringLiteral("sustained"));
flags.temperatureUnstable    = d.tempUnstable;
flags.grindIssueDetected     = d.grindHasData
    && (d.grindChokedPuck
        || d.grindYieldOvershoot
        || std::abs(d.grindFlowDeltaMlPerSec) > ShotAnalysis::FLOW_DEVIATION_THRESHOLD);
```

Notes on each row:

- `channelingDetected` deliberately uses Sustained-only (matches PR #922's `detectChannelingFromDerivative` consumer in the badge save path). Transient channeling shows in the dialog as a caution line and in `DetectorResults.channelingSeverity` as `"transient"`, but the boolean badge stays false.
- `temperatureUnstable` reads `d.tempUnstable` directly — the struct already encodes the gate (`tempStabilityChecked && !tempIntentionalStepping && tempAvgDeviationC > TEMP_UNSTABLE_THRESHOLD`) inside `analyzeShot`. No additional caller-side conjuncts needed.
- `grindIssueDetected` mirrors `ShotAnalysis::detectGrindIssue` exactly: hasData AND (chokedPuck OR yieldOvershoot OR |delta| > threshold). The struct exposes the three sub-conditions and the delta directly, so the projection is mechanical.
- `pourTruncatedDetected` and `skipFirstFrameDetected` are 1:1 with their struct fields.

## `expectedFrameCount` extension

The pre-refactor `saveShotData` and `loadShotRecordStatic` paths passed `profile->steps().size()` and `profileFrameInfoFromJson(...).frameCount` respectively to `detectSkipFirstFrame`. The pre-refactor `analyzeShot` (used by the dialog and AI advisor since PR #930) hardcoded `-1` for that parameter.

Without an `expectedFrameCount` parameter on `analyzeShot`, switching save/load to call `analyzeShot` would have *regressed* their precision — 1-frame profiles would suddenly false-positive on `skipFirstFrameDetected`. Adding the parameter (defaulted to `-1` for backwards compatibility) is the minimum extension needed for save/load to switch over without losing fidelity.

Side benefit: the dialog and AI advisor, which had been hardcoding `-1`, now also flow through callers that *can* pass the real count. The pre-existing precision divergence between save/load (precise) and dialog/AI (less precise) is therefore closed: all consumers now use the more accurate behavior when frame count is available.

## Why no A/B comparison phase

The original `design.md` called for a staged rollout with an A/B comparison logging phase before the old per-detector code was deleted. That phase was skipped because:

1. **The "old" path is a hand-rolled mirror of the same detectors `analyzeShot` already calls.** Both paths invoke `detectPourTruncated`, `buildChannelingWindows` + `detectChannelingFromDerivative`, `hasIntentionalTempStepping` + `avgTempDeviation`, `detectGrindIssue` (via `analyzeFlowVsGoal`), and `detectSkipFirstFrame`. There's no behavioral logic that lives only in the per-detector path. The cascade gates and the suppression cascade structure are identical — just expressed once vs. inline.
2. **The projection table is unit-tested table-driven** (`tst_shotanalysis::badgeProjection_*`, 15 cases). Every cell of the mapping table including the load-bearing carve-outs (Transient channeling not firing the badge, hasData defensive zero, intentional stepping suppression) has an explicit lock-in test.
3. **The `expectedFrameCount` extension closes the only meaningful divergence** between the dialog/AI path and save/load. Without it, A/B logging would have surfaced 1-frame-profile drift that we already know about and have a fix for. With it, the divergence is gone.
4. **The full test suite passes** (1793 tests, +15 new badge projection cases). Existing `tst_dbmigration`, `tst_shotsummarizer`, and end-to-end tests exercise the save/load paths via real `ShotRecord` round-trips. A regression in the projection would be visible there.

If a corpus-level regression does surface post-merge (e.g. a real shot whose recomputed badges drift on first load under the new path), that's the right time to add A/B logging — but for the corpus we have, the projection table is provably equivalent to the per-detector path.

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Per-row badge change visible to users (e.g. a shot newly fires `channelingDetected` on load) | The 15 projection unit tests lock in every cell of the mapping table; any change requires updating the corresponding test. |
| `analyzeShot` is more expensive than the sum of individual detector calls | Untrue — `analyzeShot` runs the same detectors plus a few extra struct-field assignments. The detectors are the cost; running them once vs. five times saves work, doesn't add it. |
| `ShotAnalysis::FLOW_DEVIATION_THRESHOLD` constant becomes a load-bearing public dependency for the projection | Already public (`static constexpr` in `shotanalysis.h`). The header `shotbadgeprojection.h` reads it from there; consumers depending on it must read from `ShotAnalysis`, not inline a magic number. |
| `transient` channeling now leaks into a path it didn't before | No — `DetectorResults.channelingSeverity` already exposes it via MCP `shots_get_detail`. The badge column projection deliberately drops Transient back to `false`, matching today's badge behavior. Locked in by `badgeProjection_transientChanneling_doesNotFireBadge`. |
| Schema/migration concerns | None. No schema change. The five badge columns stay; the projection just produces their values differently. |
| `expectedFrameCount` change breaks the dialog or AI advisor | Default value is `-1` (unknown, matches old hardcoded behavior). Existing `generateSummary` wrapper forwards `-1`. Only `saveShotData` and `loadShotRecordStatic` pass real counts; everywhere else falls back to the previous behavior. |

## Why not also unify with `convertShotRecord`'s `analyzeShot` call?

`convertShotRecord` already calls `analyzeShot` (post PR #933). It runs on serialization, after `loadShotRecordStatic` has already populated `record.*Detected`. So `convertShotRecord`'s call is for the structured `detectorResults` consumers (MCP), not for the badge columns. After this change, `loadShotRecordStatic` and `convertShotRecord` will each call `analyzeShot` once per shot load — that's still two calls per detail load. A future change can elevate `analyzeShot`'s output into `ShotRecord` so a single pipe through both functions reuses one pass. Out of scope here; would expand the `ShotRecord` interface and is orthogonal to the cascade-unification goal.
