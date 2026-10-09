# Shipped change reconciliation — October 8, 2026

The fix shipped as `13765dfb2` in [PR #1282](https://github.com/Kulitorum/Decenza/pull/1282). The user accepted archival after review against current main (`3456f8e77`). The old 0/31 checklist did not reflect the implementation.

## 1. Implementation and evidence

- [x] 1.1 Confirm clean-average capture, reset, plausibility guard and cup-removal fallback in src/controllers/shottimingcontroller.{h,cpp}, and matching tests in tests/tst_settling.cpp; checked against current source.
- [x] 1.2 Confirm saved-shot stoppedBy reaches the standalone block through ShotSummary and its manual/weight/volume allowlist, with tests in tst_shotsummarizer.cpp. The former live summarize overload is historical; saved-record projection is now authoritative.
- [x] 1.3 Confirm the offline tool landed as shot_eval --settling in tools/shot_eval/main.cpp, not a separate settling_replay executable; current SAW documentation describes capture and fallback behavior.
- [x] 1.4 Record historical PR/merge evidence: the reproducer was red at 38.5 g, shot 5470 replay recovered 42.4 g, the PR reports 51/51 suites and 21/21 detector regressions passing, and merge review records further settling/stop-reason tests. No corpus or test run was repeated during this bookkeeping change.

## 2. Archive readiness

- [x] 2.1 Transfer the unrecorded on-device cup-lift/stop-reason observation to verify-shipped-advisor-and-settling/tasks.md; do not mark it passed or automatically message the reporter.
- [x] 2.2 Strictly validate the reconciled requirements before CLI archival, preserving the rest of the newer SAW and advisor main specs.

## Workflow follow-up

- Archive the shipped implementation; the physical-device check remains explicitly open in verify-shipped-advisor-and-settling.
