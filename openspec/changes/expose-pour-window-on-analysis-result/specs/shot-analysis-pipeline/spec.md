# shot-analysis-pipeline

## ADDED Requirements

### Requirement: `DetectorResults` SHALL expose the pour window `analyzeShot` computed

`ShotAnalysis::DetectorResults` SHALL include `pourStartSec` and `pourEndSec` fields populated by `analyzeShot` from the same `pourStart` / `pourEnd` locals it uses internally for the suppression cascade and detector gates. These fields SHALL be the canonical pour-window values for any consumer that needs them.

`ShotSummarizer::computePourWindow` SHALL be deleted. Its sole consumer (the `markPerPhaseTempInstability` gate) SHALL read from `AnalysisResult::detectors::pourStartSec` / `pourEndSec` instead.

`ShotHistoryStorage::convertShotRecord` SHALL serialize the two new fields onto the MCP `detectorResults` JSON object so external agents have access to the same pour-window values the in-app cascade uses.

#### Scenario: Pour window matches analyzeShot's internal computation

- **GIVEN** any shot with phase markers
- **WHEN** `analyzeShot` is invoked
- **THEN** `result.detectors.pourStartSec` SHALL equal the `pourStart` value `analyzeShot` uses internally for its suppression-cascade gates
- **AND** `result.detectors.pourEndSec` SHALL equal the corresponding `pourEnd` value

#### Scenario: ShotSummarizer's per-phase temp gate uses the exposed window

- **GIVEN** a shot for which `markPerPhaseTempInstability` would run (not pour-truncated, reached extraction phase)
- **WHEN** `ShotSummarizer::summarize` or `summarizeFromHistory` runs
- **THEN** the gate's pour-window inputs SHALL come from `summary.pourStartSec` / `pourEndSec` (or equivalent cached `AnalysisResult` fields)
- **AND** `computePourWindow` SHALL no longer exist in the codebase

#### Scenario: MCP consumers see the pour window

- **GIVEN** a shot served via `shots_get_detail`
- **WHEN** the response is rendered
- **THEN** `detectorResults.pourStartSec` and `detectorResults.pourEndSec` SHALL be present and reflect the same values used internally for cascade gating
