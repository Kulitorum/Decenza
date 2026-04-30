# shot-analysis-pipeline

## ADDED Requirements

### Requirement: ShotSummarizer's detector-orchestration glue SHALL live in exactly one helper

`ShotSummarizer::summarize` (live shot) and `ShotSummarizer::summarizeFromHistory` (saved shot) SHALL each delegate their final detector-orchestration block to a single private static helper `ShotSummarizer::runShotAnalysisAndPopulate`. The helper accepts pre-extracted typed inputs (curves, markers, beverage type, analysis flags, frame info, target/final weights) plus an optional cached `AnalysisResult`, and populates the passed-in `ShotSummary`'s `summaryLines`, `pourTruncatedDetected`, and per-phase `temperatureUnstable` markers.

The two callers SHALL retain their respective input-adapter roles (live extracts from `ShotDataModel*`; history extracts from `QVariantMap` via `variantListToPoints`) but SHALL NOT contain duplicated `analyzeShot`-call + result-unpacking + `markPerPhaseTempInstability`-gating logic.

The fast-path optimization from change `dedup-ai-advisor-history-path` (reading pre-computed `summaryLines` when present on `summarizeFromHistory`'s input) SHALL be preserved by passing the cached `AnalysisResult` to the helper when applicable; the helper's `cachedAnalysis.has_value()` branch handles the rest.

#### Scenario: Live and history paths reuse the same orchestration helper

- **GIVEN** the same shot data presented to both `summarize(ShotDataModel*, ...)` and `summarizeFromHistory(QVariantMap)`
- **WHEN** the two functions run
- **THEN** both SHALL call `ShotSummarizer::runShotAnalysisAndPopulate` with equivalent inputs
- **AND** the resulting `ShotSummary` SHALL be byte-equal across the two paths (same `summaryLines`, same `pourTruncatedDetected`, same per-phase `temperatureUnstable` flags)

#### Scenario: Cached AnalysisResult fast-path is preserved through the helper

- **GIVEN** a `summarizeFromHistory` call where `shotData["summaryLines"]` is non-empty
- **WHEN** the helper is invoked with a cached `AnalysisResult` derived from those lines
- **THEN** the helper SHALL NOT re-run `analyzeShot`
- **AND** SHALL populate `summary.summaryLines` from the cache and derive `pourTruncatedDetected` from `cachedAnalysis.detectors.pourTruncated`
