# Tasks

## 1. Mock ShotDataModel

- [ ] 1.1 Add a minimal `MockShotDataModel` (file-scope class in `tests/tst_shotsummarizer.cpp` or a `tests/mock_shotdatamodel.h` header) that subclasses or duck-types the accessor methods `ShotSummarizer::summarize` reads:
  - `pressureData()`, `flowData()`, `cumulativeWeightData()`, `temperatureData()`, `temperatureGoalData()`, `conductanceDerivativeData()`
  - `phaseMarkersList()`, `pressureGoalData()`, `flowGoalData()`
- [ ] 1.2 The mock exposes setters/init helpers so test fixtures can stuff curve data: `mock.setPressure(QVector<QPointF>{...})` etc.
- [ ] 1.3 Decide between subclass vs. full QObject mock. Subclass is simpler if `ShotDataModel`'s accessors are virtual; otherwise a minimal duck-typed class works (since `summarize` takes `const ShotDataModel*` we'd need a real subclass). Pick whichever has lower friction.

## 2. Live-path tests

- [ ] 2.1 `summarize_pourTruncated_suppressesChannelingAndTempLines`: build a `MockShotDataModel` with the same puck-failure shape as the existing `pourTruncatedSuppressesChannelingAndTempLines` history test. Call `summarize(mock, profile, metadata, 18, 36)`. Assert the same line-suppression contract.
- [ ] 2.2 `summarize_abortedPreinfusion_doesNotFlagPerPhaseTemp`: mirrors the existing aborted-preinfusion history test. Asserts the per-phase temp gate.
- [ ] 2.3 `summarize_healthyShot_keepsObservations`: mirrors the existing healthy-shot history test.

## 3. Optional parity test

- [ ] 3.1 `summarize_andHistory_producesEquivalentSummary`: build the same shot data once. Run through `summarize(mock, profile, ...)` and `summarizeFromHistory(equivalentQVariantMap)`. Assert `summary.summaryLines`, `summary.pourTruncatedDetected`, and `summary.phases` are byte-equal across the two paths.
- [ ] 3.2 Skip if the mock setup is awkward — the three direct tests above already cover the live-path surface; parity is a nice-to-have, not a contract this change needs to lock in.

## 4. Verify

- [ ] 4.1 Build clean (Qt Creator MCP).
- [ ] 4.2 All existing `tst_shotsummarizer` tests pass + 3 (or 4) new ones.
- [ ] 4.3 Re-run with `-DCMAKE_BUILD_TYPE=Debug` to catch any QObject signal-emission warnings the mock triggers.
