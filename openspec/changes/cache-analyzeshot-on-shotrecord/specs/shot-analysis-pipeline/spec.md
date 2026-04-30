# shot-analysis-pipeline

## ADDED Requirements

### Requirement: Shot detail loads SHALL run `analyzeShot` exactly once

When a shot is loaded via `ShotHistoryStorage::loadShotRecordStatic` and immediately serialized via `ShotHistoryStorage::convertShotRecord` (the canonical detail-load path), the application SHALL invoke `ShotAnalysis::analyzeShot` exactly once for that shot. The `AnalysisResult` produced by `loadShotRecordStatic`'s recompute SHALL be cached on the returned `ShotRecord` (via an optional field), and `convertShotRecord` SHALL read from that cache when present instead of running `analyzeShot` a second time.

When `convertShotRecord` is called on a `ShotRecord` that was NOT produced by `loadShotRecordStatic` — direct construction in `ShotHistoryExporter`, in tests, or any other path that bypasses the load helper — the cache MAY be absent. In that case, `convertShotRecord` SHALL fall back to running `analyzeShot` inline so behavior remains correct end-to-end.

The cached `AnalysisResult` SHALL be invalidated (cleared / reset) if any input curve on the `ShotRecord` is mutated after load. Today no caller mutates `ShotRecord` between `loadShotRecordStatic` and `convertShotRecord`, but the field's docstring SHALL document this invariant so future callers don't introduce a stale-cache bug.

`analyzeShot`'s signature, the badge column projection (`decenza::applyBadgesToTarget`), the prose `summaryLines`, and the structured `detectorResults` JSON SHALL all be unchanged. This is a pure caller-side dedup.

#### Scenario: Standard detail-load path runs analyzeShot once

- **GIVEN** a shot record loaded via `loadShotRecordStatic`
- **WHEN** the same `ShotRecord` is then passed to `convertShotRecord`
- **THEN** `loadShotRecordStatic` SHALL have populated `record.cachedAnalysis` with the `AnalysisResult` it computed
- **AND** `convertShotRecord` SHALL read `summaryLines` and `detectorResults` from the cached struct without invoking `ShotAnalysis::analyzeShot` itself

#### Scenario: Direct-construction caller falls back to inline analyzeShot

- **GIVEN** a `ShotRecord` constructed directly (e.g. `ShotHistoryExporter`) without `cachedAnalysis`
- **WHEN** `convertShotRecord(record)` is invoked
- **THEN** `convertShotRecord` SHALL invoke `ShotAnalysis::analyzeShot` inline
- **AND** the resulting `summaryLines` and `detectorResults` SHALL be identical to what the cached path would produce for the same input

#### Scenario: Cached and fallback paths produce equivalent output

- **GIVEN** two equivalent `ShotRecord`s for the same shot, `A` with `cachedAnalysis` populated and `B` without
- **WHEN** `convertShotRecord(A)` and `convertShotRecord(B)` are both invoked
- **THEN** the resulting QVariantMap's `summaryLines`, `detectorResults`, and all five badge boolean fields SHALL be byte-equal across the two calls
