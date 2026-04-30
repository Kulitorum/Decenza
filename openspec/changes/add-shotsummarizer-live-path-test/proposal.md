# Change: Add direct unit test coverage for `ShotSummarizer::summarize()` (live path)

## Why

`tst_shotsummarizer.cpp` currently tests only `ShotSummarizer::summarizeFromHistory(QVariantMap)` — the saved-shot path. The live path `ShotSummarizer::summarize(ShotDataModel*, profile, metadata, doseWeight, finalWeight)` has no direct unit test.

The live path is exercised indirectly when the AI advisor opens on an in-progress shot, but that's an end-to-end manual test, not a regression lock. After the cascade-unification work and the I (`dedup-shotsummarizer-input-adapter`) helper extraction, both paths converge on the same `runShotAnalysisAndPopulate` orchestration — but the input adapter on the live side (extracting from `ShotDataModel*`) is unique and could regress without surfacing in the existing test suite.

The reason there's no direct test today is that `ShotDataModel` is a heavy QObject with many signals and accessor methods, and stubbing it cleanly is non-trivial. This change adds a minimal `MockShotDataModel` (or uses a lightweight subclass exposing pre-stuffed curve data) to cover the live path with a few canonical scenarios.

## What Changes

- **ADD** a `MockShotDataModel` test fixture in `tests/tst_shotsummarizer.cpp` (or a separate header file in `tests/`). It exposes the `pressureData()`, `flowData()`, `cumulativeWeightData()`, `temperatureData()`, `temperatureGoalData()`, `conductanceDerivativeData()`, `phaseMarkersList()`, `pressureGoalData()`, `flowGoalData()` methods returning pre-stuffed test data.
- **ADD** 3 live-path test cases mirroring the existing history-path cases:
  - `summarize_pourTruncated_suppressesChannelingAndTempLines` (live)
  - `summarize_abortedPreinfusion_doesNotFlagPerPhaseTemp` (live)
  - `summarize_healthyShot_keepsObservations` (live)
- **OPTIONAL** add a parity test that asserts `summarize()` and `summarizeFromHistory()` produce equivalent `ShotSummary` for the same shot data. Locks in that the two adapters produce the same outputs.

## Impact

- Affected specs: none (test-only change).
- Affected code: `tests/tst_shotsummarizer.cpp` (test additions), possibly a new `tests/mock_shotdatamodel.h`.
- Test count: +3 to +4 (depending on whether the parity test lands).
- Risk: low. Pure test surface expansion. The mock might mis-implement a `ShotDataModel` accessor signature; CI catches that immediately.

## Why this is lower priority

The live path is exercised by every real shot in production. A bug in it would surface immediately to the user (wrong AI advisor prompt, mis-rendered Shot Summary dialog). The test gap is a hygiene issue, not a real risk surface — it's worth filling but it's not load-bearing.

## Out of scope

- Any production code changes. This is test-only.
- Mocking `Profile*` and `ShotMetadata` beyond what's needed for the helper to run. Use the existing minimal-Profile fixtures the storage tests use.
