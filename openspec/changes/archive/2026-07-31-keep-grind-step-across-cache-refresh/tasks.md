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

## 3. Remove the distinct-value cache

- [x] 3.1 Replace `requestDistinctCache()` / `requestDistinctValueAsync()` and the cache members
      with one `queryDistinctList()` that reads the live database
- [x] 3.2 Route all nine `getDistinct*()` getters through it; drop `invalidateDistinctCache()` and
      its nine call sites and the `distinctCacheReady()` signal
- [x] 3.3 Drop the QML re-evaluation protocol: `distinctCacheVersion` (`GrindRowSource`,
      `PostShotReviewPage`), `_distinctVersion` (`ChangeBeansDialog`), and `GrindPickerDialog`'s
      `_cacheConn` + `_autoTextPendingHistory` state machine
- [x] 3.4 Gate `rpmStep` on `rpmCapable` — only `rpmRowsFor()` reads it, and the picker only calls
      that for an RPM grinder
- [x] 3.5 Confirm no other DB cache of this kind exists (`cachedAnalysis` is a per-record compute
      memo, `m_canonicalCache` is HTTP — both keep)

## 4. Review cleanups

- [x] 4.1 `#1724` → `#1713` in five places — #1724 is an unrelated merged PR
- [x] 4.2 Fix the spliced comment on the derivation, and cut the postmortem of the reverted draft
- [x] 4.3 Drop the `qsizetype* outCount` out-param: return the sorted values, let each caller
      derive and count
- [x] 4.4 Give `grinderWideRpmStep()` the `prepare()` check its sibling has; state its non-empty
      model precondition
- [x] 4.5 One `grinderModelMatchSql()` for the case-folding predicate that was hand-copied six times
- [x] 4.6 Key `reportGrindStep`'s dedupe per model — a single scalar alternating between two live
      `GrindRowSource`s deduped nothing; report the `!m_ready` return instead of answering 0 silently
- [x] 4.7 Move `hasColumn`/`hasTable`/`hasIndex`/`initAndClose` into `ShotRowFixtures` (renamed from
      `ShotFixtures` to match `ShotCurveFixtures`); replace the three `20 x msleep(25)` drain loops
      with `isDbWorkIdle()` polling — a timer standing in for a condition, which CLAUDE.md forbids

## 5. Guardrails

- [x] 5.1 `CLAUDE.md`: rewrite the main-thread DB I/O rule around user impact and machine
      operation rather than a blanket ban, since the blanket reading is what produced the cache
- [x] 5.2 `CLAUDE.md`: "Complexity has to come with a measurable win, stated in units the USER
      feels", plus the two corollaries (re-derive a justification when the design moves; needing
      fault injection to reach a branch is a stop sign)
- [x] 5.3 `CLAUDE.md`: do not engineer around migrations — they run once, with the app stopped

## 6. Ship

- [x] 6.1 Revert the over-built draft in full (resident map, covering index, schema version 36,
      supersession guard, failed-key set, distinct-cache repair)
- [x] 6.2 Push to PR #1725 and re-run `/pr-review-toolkit:review-pr` (three rounds; every
      round found real defects, the last two in the previous round's fixes)
- [x] 6.3 Archive + spec-sync as the final commit on the PR
