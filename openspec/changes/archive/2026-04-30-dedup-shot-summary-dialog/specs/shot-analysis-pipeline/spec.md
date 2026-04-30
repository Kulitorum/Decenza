# shot-analysis-pipeline

## ADDED Requirements

### Requirement: Shot Summary dialog SHALL prefer pre-computed `summaryLines` when present

The in-app Shot Summary dialog (`ShotAnalysisDialog.qml`) SHALL render its observation list from the `summaryLines` field of its input `shotData` map when that field is a non-empty list. The dialog SHALL invoke the `MainController.shotHistory.generateShotSummary(shotData)` Q_INVOKABLE bridge ONLY as a fallback when `summaryLines` is absent or empty (e.g. for legacy callers whose `shotData` did not flow through `ShotHistoryStorage::convertShotRecord`).

The dialog SHALL render identical prose lines (same text, same `type` values, same order) regardless of which path produced the list. The fallback exists for backwards compatibility with legacy entry points; it MUST NOT introduce visible differences.

#### Scenario: Modern shotData carries pre-computed summaryLines

- **GIVEN** a shot record loaded via `ShotHistoryStorage::convertShotRecord` (so `shotData.summaryLines` is populated by the `analyzeShot` call inside `convertShotRecord`)
- **WHEN** the user opens the Shot Summary dialog on that shot
- **THEN** the dialog SHALL render the lines from `shotData.summaryLines` directly
- **AND** the dialog SHALL NOT invoke `MainController.shotHistory.generateShotSummary(shotData)` (no second `analyzeShot` pass)

#### Scenario: Legacy shotData without summaryLines falls back to the wrapper

- **GIVEN** a `shotData` map whose `summaryLines` field is missing or an empty list
- **WHEN** the user opens the Shot Summary dialog
- **THEN** the dialog SHALL invoke `MainController.shotHistory.generateShotSummary(shotData)`
- **AND** SHALL render the returned line list with the same per-line dot colors as before this change

#### Scenario: Identical rendering across paths

- **GIVEN** two shotData maps `A` and `B` representing the same shot, where `A.summaryLines` is populated and `B.summaryLines` is empty
- **WHEN** the dialog opens on each
- **THEN** both renderings SHALL contain the same observation lines in the same order with the same `type` values
