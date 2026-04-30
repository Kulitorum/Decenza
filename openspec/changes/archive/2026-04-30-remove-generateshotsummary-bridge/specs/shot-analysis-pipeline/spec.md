# shot-analysis-pipeline

## REMOVED Requirements

### Requirement: ~~`ShotHistoryStorage::generateShotSummary` Q_INVOKABLE bridge~~

**Reason for removal:** Post `dedup-shot-summary-dialog` (PR #934) the in-app Shot Summary dialog reads `shotData.summaryLines` directly from `convertShotRecord`'s output. The Q_INVOKABLE bridge `ShotHistoryStorage::generateShotSummary` is now only the dialog's *fallback* path, hit only when `shotData.summaryLines` is missing — a case that doesn't occur in production because every QVariantMap reaching the dialog flows through `convertShotRecord` (which always populates `summaryLines` post-PR #933).

The bridge has no live production callers and represents implicit-drift surface: it does its own `profileFrameInfoFromJson` parse, its own `getAnalysisFlags` lookup, and reconstructs `QList<HistoryPhaseMarker>` from the QVariantMap — three reconstruction paths that could subtly diverge from `convertShotRecord`'s.

**Migration:** External callers that need to convert a serialized shot back to prose lines should:
1. Load the shot via `ShotHistoryStorage::requestShot` and let `convertShotRecord` populate `summaryLines` on the resulting QVariantMap, OR
2. Construct a `ShotRecord`, call `ShotAnalysis::analyzeShot` directly, and read `.lines` from the result.

The `ShotAnalysis::generateSummary` static method (the `analyzeShot(...).lines` wrapper) is unaffected and remains available for callers that have raw curve data.

## ADDED Requirements

### Requirement: Shot Summary dialog SHALL render `shotData.summaryLines` with an empty fallback

After the bridge is removed, `ShotAnalysisDialog.qml` SHALL bind its `analysisLines` property to `shotData.summaryLines` directly when present, falling back to an empty list `[]` when absent. The dialog SHALL NOT invoke any C++ helper that recomputes prose from a serialized shot — the canonical computation site is `convertShotRecord`'s `analyzeShot` pass.

When `shotData.summaryLines` is absent (a hypothetical edge case for shots that didn't flow through `convertShotRecord`), the dialog SHALL render its "Shot Summary" header with no observation lines, rather than risk a divergent reconstruction. This is preferable UX to risking subtly wrong prose; if a future product decision needs a "couldn't analyze this shot" message, it can be added at the dialog level without resurrecting the bridge.

#### Scenario: Modern shotData renders directly

- **GIVEN** a shotData QVariantMap produced by `convertShotRecord`, with `summaryLines` populated
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL render the prose lines from `shotData.summaryLines` directly
- **AND** the dialog SHALL NOT invoke any Q_INVOKABLE method on `ShotHistoryStorage`

#### Scenario: shotData without summaryLines renders empty

- **GIVEN** a shotData QVariantMap whose `summaryLines` field is absent or not a non-empty list
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL render its "Shot Summary" header with no observation lines
- **AND** the dialog SHALL NOT crash, fall back to recomputation, or display unrelated content
