# Change: Dedup Shot Summary dialog by reading pre-computed `summaryLines`

## Why

After PR #933, `ShotHistoryStorage::convertShotRecord` runs `ShotAnalysis::analyzeShot` on every shot conversion and emits `summaryLines` (prose) plus a nested `detectorResults` JSON object on every shot record. The QML Shot Summary dialog still calls `MainController.shotHistory.generateShotSummary(shotData)` every time it becomes visible, which re-runs the entire `analyzeShot` pipeline on the same `shotData` map that already carries `summaryLines`. That's two full passes over the curve vectors per dialog open — pure redundancy. PR #933's description acknowledged this as a deliberate follow-up.

## What Changes

- **MODIFY** `qml/components/ShotAnalysisDialog.qml` so its analysis-lines binding reads `shotData.summaryLines` directly when the field is present, falling back to the existing `MainController.shotHistory.generateShotSummary(shotData)` invocation only when it's absent (e.g. legacy entry points that bypass `convertShotRecord`).
- **NO change** to `ShotHistoryStorage::generateShotSummary` itself — keep the Q_INVOKABLE bridge. It's still the right entry point for any QML caller that doesn't already have `summaryLines` on its `shotData`. Removing it would break those callers.
- **NO change** to `ShotAnalysis::analyzeShot` or `ShotAnalysis::generateSummary` — pure consumer-side dedup.

## Impact

- Affected specs: `shot-analysis-pipeline` (new requirement: dialog SHALL prefer pre-computed `summaryLines` when present).
- Affected code: `qml/components/ShotAnalysisDialog.qml`, optionally `qml/pages/ShotDetailPage.qml` if it has its own dialog instance with the same binding.
- User-visible behavior: identical. The same prose lines render in the same order with the same dot colors.
- Performance: one fewer `analyzeShot` pass per dialog open (~bounded linear scans over a few hundred curve samples per shot — small win, but reduces the "compute is the source of truth" surface).
