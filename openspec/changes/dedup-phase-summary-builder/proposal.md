# Change: Dedup the per-marker `PhaseSummary` builder between `summarize()` and `summarizeFromHistory()`

## Why

`ShotSummarizer::summarize()` (live shot) and `ShotSummarizer::summarizeFromHistory()` (saved shot) each contain a ~50-line per-marker loop that builds `PhaseSummary` entries for the AI advisor's per-phase prompt block. The two loops compute the same metrics (`avgPressure`, `avgFlow`, `avgTemperature`, `weightGained`, `pressureAt{Start,Middle,End}`, etc.) using the same helper math (`findValueAtTime`, `calculateAverage`, `calculateMax`, `calculateMin`).

PR #929 deduped only the no-markers fallback (`makeWholeShotPhase`). The full per-marker loop is the bigger duplicate and the more drift-prone — every time someone adds a per-phase metric (e.g. PR #763 added overall shot peaks), they have to update both loops by hand. The two loops also differ in subtle ways that only show up under audit:

- The live path uses `phase.avgTemperature` but the history path doesn't bother with `phase.maxTemperature`/`minTemperature` (neither path sets them today, but a future addition would have to land in two places).
- The live path's `marker.transitionReason` plumbing has slightly different defaults from the history path.

Closing the duplication into a single helper makes future per-phase metric additions a one-place change.

## What Changes

- **ADD** a private static helper `ShotSummarizer::buildPhaseSummariesForRange(...)` that takes pre-extracted typed inputs (the four `QVector<QPointF>` curves + a `QList<HistoryPhaseMarker>` + the total duration) and returns `QList<PhaseSummary>`. Pure function.
- **MODIFY** `summarize()` to extract its curves from `ShotDataModel*` (it already does) and call the new helper instead of the inline loop.
- **MODIFY** `summarizeFromHistory()` to call the helper instead of its inline loop. Curves are already extracted from the `QVariantMap` earlier in the function.
- **NO change** to `PhaseSummary`'s shape, `analyzeShot`'s inputs, or any consumer of `ShotSummary::phases`. Pure caller-side dedup.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: per-marker `PhaseSummary` construction lives in exactly one helper).
- Affected code: `src/ai/shotsummarizer.{h,cpp}`. New private static helper; both call sites simplified by ~50 lines each.
- User-visible behavior: identical AI prompts, identical `ShotSummary::phases` content.
- Performance: identical (the helper just hoists the math; the work is the same).
- Risk: medium. The two loops have small documented differences (the history path skips degenerate phases via `endTime <= startTime continue`, and stores the marker even when the phase is skipped). The helper must preserve both behaviors. A regression test asserting both paths produce identical phases for the same shot is part of the work.

## Out of scope

- The live `summarize()` path's prep (extracting `pressureData` etc. from `ShotDataModel*`) and the history path's `variantListToPoints` adapter stay separate. Those are input adapters, not phase-builder logic. See change `dedup-shotsummarizer-input-adapter` for that.
