# Tasks

## 1. Commit a pair that a shot actually produced

- [x] 1.1 In `addSawPerPairEntry` (`src/core/settings_calibration.cpp`), make `medianLag` the
      median of the batch's per-shot lags, and commit the `(drip, flow)` of the batch shot whose
      lag is closest to it. `medianOver` stays an independent median — it gates the auto-reset
      and is never divided. Verify: the committed entry's `drip` and `flow` both appear in the
      batch's raw entries.
- [x] 1.2 Confirm the outlier gate now compares against a lag some shot had, since it reads the
      same `medianLag`. Verify: no separate gate change was needed, and
      `dispersionGateMeasuresAgainstTheMedianLagNotTheMedianOfMedians` fails against the old
      reference.
- [x] 1.5 Drop a batch in which no shot had a usable flow, rather than falling back to the
      composite pair with a printed lag of 0.000 s. Verify:
      `batchWithNoUsableFlowIsDroppedNotCommitted` asserts the warning and an empty history.
- [x] 1.3 Confirm the commit log line already carries `(drip, flow)` beside the lag, so a
      submitted log shows the two describe one shot. Verify: `[SAW][Learning] Committed median
      lag=… (drip=… flow=…)` — unchanged, and `drip/flow` equals the printed lag by
      construction, since `medianLag` is read off the committed pair.
- [x] 1.4 Confirm no reader needed changing: `sawLearningEntriesFor`, `getExpectedDripFor`,
      `sawLearnedLagFor` and `recomputeGlobalSawBootstrap` all still read `drip` and `flow`. Verify: no diff outside the
      median computation and the commit step.

## 2. Tests

- [x] 2.1 Add `committedEntryIsOneShotsOwnDripAndFlow` to `tests/tst_saw_settings.cpp` for a
      batch whose median-drip shot is NOT its median-flow shot (drips 1.30/1.90/1.53 with flows
      1.87/2.10/1.60 — real data from this maintainer's device, where the old code stored a lag
      of 0.818 s against a true median lag of 0.905 s). Verify: the committed entry equals one of
      the three input pairs, and its implied lag is the batch median lag.
- [x] 2.2 Break the fix (restore independent medians) and watch 2.1 go red before keeping it.
      Verify: failed with the old code, passes with the new. Done — the first run also caught a
      wrong expectation in the test itself (0.956 s vs the true 0.905 s).
- [x] 2.3 Full suite green via `mcp__qtcreator__run_tests` scope `all`.

## 3. The evaluation instrument

- [x] 3.1 Extend the corpus to 250 shots (`tools/saw_replay/data/baseline_extended.json`),
      harvested from a full `/api/database` download. Verify: 250 rows, ids unique,
      chronologically ordered, both baskets and all three scale keys present.
- [x] 3.2 Record that the device database has been renumbered since the first harvest, so the
      two parts are matched by timestamp rather than id and the old ids are carried as
      `legacy_id` offset by +10000. Verify: noted in the corpus `_comment` and in `analysis.md`.
- [x] 3.3 Fix `tools/saw_parity` to pass the basket key — its default argument was collapsing
      every shot into one pool. Make `--sim` optional so the production-MAE table runs without
      the simulator, and delete the `wipeAllSawState` loop that split pool keys on `::` and
      matched nothing once the basket segment landed. Verify: builds, and the report names
      `basket_segment=used`.
- [x] 3.4 Record the baseline for the shipped model. Verify: table in `analysis.md`.

## 4. Measure this change against that baseline

- [x] 4.1 Replay the corpus with the fix and report per-bucket MAE and worst-case error against
      3.4. Verify: table in `analysis.md`.
- [x] 4.2 State the verdict plainly, including the high-flow regression. Verify: `analysis.md`
      and `proposal.md` both say the change ships on correctness and is not a performance win.
- [x] 4.3 Withdraw the ad-hoc Python simulator's figures (−8.5% / +5.9%), which reproduced in
      neither magnitude nor sign. Verify: withdrawn in `proposal.md` and `analysis.md`.

## 5. The basket segment

- [x] 5.1 Measure what the basket segment buys, via `saw_parity --ignore-basket`. Verify: table
      in `analysis.md` covering the whole corpus and the two-basket window.
- [x] 5.2 Record that the n=3 finding behind `key-saw-learning-by-basket` has not reproduced,
      and that the segment stays anyway — the two baskets measured are both 58 mm and differ
      mainly in wall shape, which is a weak test. Verify: stated in `proposal.md` with the
      condition for revisiting.

## 6. Review findings

- [x] 6.0 `/pr-review-toolkit:review-pr`. Fixed: the archived probe matched both `887` and
      `10887` (different shots) and reported the wrong one; `medianDrip`/`medianFlow` no longer
      held medians and were renamed; the empty-lag fallback committed the composite silently
      and now drops the batch; `saw_parity` now reports corpus rows with no basket key rather
      than presenting a collapsed run as a keyed one; the lag/filter derivation was duplicated
      and is now built once; `globalSawBootstrapLag` was named where
      `recomputeGlobalSawBootstrap` was meant; the reader inventory omitted
      `sawLearningEntriesFor`, the reader that feeds the live stop threshold; two tautological
      assertions deleted; three tests added for behaviour the change moved but nothing covered.

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/SAW_LEARNING.md`: the storage-schema table, the commit
      pseudocode, and the "why batched, with median" section. Verify: they match the code.
- [x] 7.2 No wiki entry. Nothing here is discoverable or actionable by a user — stop-at-weight
      behaviour is unchanged in kind.

## 8. Ship

- [x] 8.1 Open a PR.
- [x] 8.2 Run `/pr-review-toolkit:review-pr` and address what it finds.
- [ ] 8.3 Archive the change (`openspec archive`) as the final commit on the PR.
