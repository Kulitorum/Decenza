# Change: Remove `ShotHistoryStorage::generateShotSummary` Q_INVOKABLE

## Why

`ShotHistoryStorage::generateShotSummary(QVariantMap shotData)` is a Q_INVOKABLE bridge that converts a serialized shot back into typed inputs and runs `ShotAnalysis::generateSummary`. Before PR #934 (dialog dedup), it was the canonical path the in-app Shot Summary dialog took to compute prose lines.

Post-#934, the dialog reads `shotData.summaryLines` directly from `ShotHistoryStorage::convertShotRecord`'s output. `generateShotSummary` is now only the *fallback* path the dialog hits when `summaryLines` is missing — a case that doesn't occur in production because every QVariantMap reaching the dialog flows through `convertShotRecord`, which always populates `summaryLines` (post-#933).

Keeping a Q_INVOKABLE that has no live callers and no documented preconditions for "when is the fallback hit?" is implicit-drift surface: the bridge does its own `profileFrameInfoFromJson` parse, its own `getAnalysisFlags` lookup, and reconstructs `QList<HistoryPhaseMarker>` from the `QVariantMap` — three independent reconstruction paths that could subtly diverge from `convertShotRecord`'s.

This change deletes the Q_INVOKABLE and simplifies the dialog binding to drop the fallback. If someone in the future legitimately needs to convert a serialized shot back to prose lines without going through `convertShotRecord`, they should add a more explicit helper at that time, not lean on a vestigial bridge.

## What Changes

- **REMOVE** `ShotHistoryStorage::generateShotSummary(const QVariantMap&)` from both the header and implementation.
- **MODIFY** `qml/components/ShotAnalysisDialog.qml` `analysisLines` binding to drop the fallback branch:
  ```qml
  property var analysisLines: {
      if (!analysisDialog.visible) return []
      return Array.isArray(shotData?.summaryLines) ? shotData.summaryLines : []
  }
  ```
  Empty-fallback (`[]`) preserves dialog correctness for the (now hypothetical) case where `shotData.summaryLines` is absent — the dialog renders a "Shot Summary" header with no observation lines, which is correct for a shot the analyzer couldn't process. Better than rendering wrong prose from a parallel reconstruction path.
- **REMOVE** the corresponding test cases in `tst_shotanalysis.cpp` / `tst_shotsummarizer.cpp` that exercised the QVariantMap-roundtrip path through `generateShotSummary`. The remaining `analyzeShot` and `convertShotRecord` tests already cover the canonical pipeline.
- **NO change** to `analyzeShot`, `convertShotRecord`, the badge projection, or the AI advisor's `summarizeFromHistory` path.

## Impact

- Affected specs: `shot-analysis-pipeline` — REMOVED requirement: the `generateShotSummary` Q_INVOKABLE bridge no longer exists; the dialog SHALL read `shotData.summaryLines` directly with an empty fallback.
- Affected code: `src/history/shothistorystorage.{h,cpp}` (deletion), `qml/components/ShotAnalysisDialog.qml` (drop fallback branch), tests covering the deleted function.
- User-visible behavior: identical for shots that flow through `convertShotRecord` (i.e. all production shots). Hypothetical legacy shots with empty `summaryLines` will see an empty dialog body instead of recomputed prose — preferable to risking divergent output.
- Risk surface: low. Confirmed via `grep` that the only caller is `ShotAnalysisDialog.qml`'s fallback. If a third caller emerges during the change, surface it explicitly and consider keeping the bridge.

## Out of scope

- The fallback-empty case could optionally show a "couldn't analyze this shot" message instead of an empty body. UX decision; defer to a separate UI follow-up if anyone wants it.
- `ShotAnalysis::generateSummary` (the `analyzeShot(...).lines` wrapper) is independent and stays. It's still used by `ShotSummarizer` and the slow path of `summarizeFromHistory`.
