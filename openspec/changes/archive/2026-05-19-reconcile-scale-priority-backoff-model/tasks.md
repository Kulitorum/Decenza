## 1. Spec reconciliation (this change's delta)

- [x] 1.1 Apply the `ble-connection-priority` delta in this change to `openspec/specs/ble-connection-priority/spec.md` (done automatically by `openspec archive` when this change is archived after review).
- [x] 1.2 Replace the placeholder `## Purpose` ("TBD - created by archiving…") in `openspec/specs/ble-connection-priority/spec.md` with an accurate purpose statement (done in this branch as cleanup of the observe-mode archive output).

## 2. Retire the superseded active changes (no delta application)

- [x] 2.1 Move `openspec/changes/scale-ble-priority-backoff/` to `openspec/changes/archive/2026-05-19-scale-ble-priority-backoff-SUPERSEDED/` via `git mv` — NOT via `openspec archive` (its deltas encode the single-cause framing + mid-shot-bounce remediation and MUST NOT be applied to canonical specs).
- [x] 2.2 Move `openspec/changes/scale-priority-backoff-preheat-persist/` to `openspec/changes/archive/2026-05-19-scale-priority-backoff-preheat-persist-SUPERSEDED/` the same way.
- [x] 2.3 Add a `SUPERSEDED-BY.md` marker in each archived dir pointing to `reconcile-scale-priority-backoff-model`, stating their core thesis is **vindicated** (genuine dual-HIGH contention on weak hardware is real → BALANCED mitigates it) but **refined**: #1176 has two causes — the weight-sample dedup (cause 1, fixed #1224) plus genuine contention (cause 2, the latch's job); and the mid-shot bounce was replaced by latch-only/idle-bounce (#1226). So the supersession is discoverable in history.

## 3. Verify

- [x] 3.1 `openspec validate reconcile-scale-priority-backoff-model` passes (deltas parse; every requirement has ≥1 scenario).
- [x] 3.2 `openspec list` shows the two superseded changes gone from active and `reconcile-scale-priority-backoff-model` present; no scale-priority change remains active that asserts backoff fixes #1176.
- [x] 3.3 Confirm no code changes are required (the implementation shipped via #1224 and #1226); this change is spec/documentation reconciliation only.
