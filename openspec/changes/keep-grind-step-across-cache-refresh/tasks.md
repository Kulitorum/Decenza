## 1. Derive live

- [x] 1.1 Give `grinderWideStep()` an empty-model branch (drop the equipment join, pool every
      grinder) and a `qsizetype* outCount` for the log line's sample count
- [x] 1.2 Point `grindStepForGrinder()` at `grinderWideStep(m_db, ...)`; delete the
      `getDistinctGrinderSettingsForGrinder()` round-trip and its cold-cache return
- [x] 1.3 Point `grindRpmStepForGrinder()` at `grinderWideRpmStep(m_db, ...)`; delete its
      hand-rolled `m_distinctCache` lookup and `requestDistinctValueAsync()` call
- [x] 1.4 Record the measured cost at the derivation, median AND worst, on a real database and a
      multiple of one

## 2. Tests

- [x] 2.1 Extract `withRawDb` / `ShotRow` / `insertShot` to `tests/shotrowfixtures.h`; collapse the
      four hand-copied `withRawDb` bodies onto it (three had dropped its open assertion, one set
      `foreign_keys` the others did not)
- [x] 2.2 `grindStepSurvivesInvalidation` — the regression test; reads once after an invalidation
- [x] 2.3 `grindStepAgreesWithDialingContext`, `grindStepFoldsModelCaseAndWhitespace`,
      `grindStepEmptyModelUsesFullHistory`, `grindStepThinHistoryReturnsZero`
- [x] 2.4 Full suite green (110/110)

## 3. Guardrails

- [x] 3.1 `CLAUDE.md`: rewrite the main-thread DB I/O rule around user impact and machine
      operation rather than a blanket ban, since the blanket reading is what produced the cache
- [x] 3.2 `CLAUDE.md`: "Complexity has to come with a measurable win, stated in units the USER
      feels", plus the two corollaries (re-derive a justification when the design moves; needing
      fault injection to reach a branch is a stop sign)
- [x] 3.3 `CLAUDE.md`: do not engineer around migrations — they run once, with the app stopped

## 4. Ship

- [x] 4.1 Revert the over-built draft in full (resident map, covering index, schema version 36,
      supersession guard, failed-key set, distinct-cache repair)
- [ ] 4.2 Push to PR #1725 and re-run `/pr-review-toolkit:review-pr`
- [ ] 4.3 Archive + spec-sync as the final commit on the PR
