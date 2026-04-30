# Tasks

## 1. Mock ShotDataModel

- [x] 1.1 The proposal contemplated either a subclass mock or a duck-typed mock; in practice the simplest path was to construct a real `ShotDataModel` and feed test data through its existing public ingestion API (`addSample`, `addWeightSample`, `addPhaseMarker`, `computeConductanceDerivative`). No production-code change, no friend declaration, no MockShotDataModel header — just a small `populateLiveShot()` builder helper colocated with the tests.
- [x] 1.2 Builder helper: `populateLiveShot(model, samples, phases, weightSamples)` accepts a flat `LiveSample` vector (`t/pressure/flow/temperature/pressureGoal/flowGoal/temperatureGoal/isFlowMode`) plus a phase-marker tuple list, calls the corresponding `ShotDataModel` setters, and finishes with `computeConductanceDerivative()` so the dC/dt curve is in place.
- [x] 1.3 No subclass needed — a real `ShotDataModel` instance is small enough to construct per-test and disposes via QObject parent ownership when the test method returns.

## 2. Live-path tests

- [x] 2.1 `summarize_pourTruncated_suppressesChannelingAndTempLines_live` — mirrors the history-path puck-failure test. Asserts `pourTruncatedDetected` fires and the cascade suppresses both channeling and temperature-drift lines on the live path.
- [x] 2.2 `summarize_abortedPreinfusion_doesNotFlagPerPhaseTemp_live` — mirrors the aborted-preinfusion history test. Asserts the `reachedExtractionPhase` gate suppresses per-phase temperature markers on the live path.
- [x] 2.3 `summarize_healthyShot_keepsObservations_live` — mirrors the healthy-shot history test. Asserts the live path doesn't over-suppress observations on a clean 9-bar shot and emits a verdict line.

## 3. Optional parity test

- [ ] 3.1 Live ↔ history equivalence test (`summarize` and `summarizeFromHistory` produce byte-equal `ShotSummary` for the same data). Skipped per proposal note ("nice-to-have, not a contract this change needs to lock in"). The three direct live-path tests above cover the live-path adapter surface; an equivalence test would primarily protect against post-helper drift, which `runShotAnalysisAndPopulate` (PR #945, I) structurally prevents by funneling both paths through one orchestration call.

## 4. Verify

- [x] 4.1 Build clean (Qt Creator MCP, 0 errors / 0 warnings).
- [x] 4.2 All existing tests pass + 3 new live-path tests (1809 total, up from 1806).
- [ ] 4.3 Re-run with Debug build for QObject signal-emission warnings — deferred. The MCP run reports 0 warnings; `addSample` does not emit signals (the production code defers signaling to the 33 ms flush timer that doesn't run in tests).
