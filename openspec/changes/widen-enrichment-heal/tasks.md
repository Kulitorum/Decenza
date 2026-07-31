# Tasks

## 1. Widen the predicate

- [x] 1.1 Change the heal's signature from "burrs empty→named, everything else equal" to "every differing component went empty→named", covering grinder brand/model/burrs, basket brand/model and the canonical puck-prep string.
- [x] 1.2 Prefer a shared `isEnrichmentOf()` helper called by BOTH the live rule (`equipmentstorage.cpp:1547-1553`) and the heal, so the two cannot drift a third time. Fall back to widening the SQL in place only if loading both packages per candidate pair proves awkward — record which was chosen and why.
- [x] 1.3 If the SQL path is taken, keep the folding Unicode-correct. SQLite's `LOWER()` is ASCII-only, which is why identity matching already does its comparison in C++ (`equipmentstorage.cpp:1096-1103`).
- [x] 1.4 Keep the grinder-less exception: a superseded package with NO grinder is never folded into a successor that has one.
- [x] 1.5 Rename the function if "enrichment fork" no longer describes it accurately.

## 2. Migration

- [x] 2.1 Add a new migration running the widened heal. Migration 35's version is already stamped on databases that reached it and cannot be reused.
- [x] 2.2 Leave migration 35 in place and unchanged — most installed devices are on stable v2.0.0 at schema ≤ 34 and will run both, which is harmless because the widened predicate is a superset.
- [x] 2.3 Gate the version bump on the backfill landing, per CLAUDE.md.
- [x] 2.4 Reuse migration 35's `DbWriteTxn` handling and its `query.finish()` before taking the write lock — the read-then-write hazard it documents applies identically.

## 3. Logging

- [x] 3.1 Log each fold with both package ids, **which components were filled in**, and the shots/bags/recipes moved. The component list is what would have made this diagnosable without a database dump.
- [x] 3.2 Keep the "ran and healed nothing" line.
- [x] 3.3 Verify with `python3 scripts/check_log_markers.py`.

## 4. Tests

- [x] 4.1 Add to `tst_equipment`, not a new target. Break each assertion and watch it go red before keeping it.
- [x] 4.2 Fork caused by recording a BASKET (grinder and burrs identical, basket empty→named) → folded. This is the maintainer's case and the one migration 35 misses.
- [x] 4.3 Fork caused by recording a puck prep → folded.
- [x] 4.4 Several components filled at once (basket AND puck prep) → folded.
- [x] 4.5 Burr swap (two non-empty values) → NOT folded.
- [x] 4.6 Changed basket (two non-empty values) → NOT folded.
- [x] 4.7 No supersession between the two → NOT folded.
- [x] 4.8 Grinder-less superseded package → NOT folded.
- [x] 4.9 Re-run on a healed database → folds nothing, reports zero.
- [x] 4.10 A database already at migration 35 with a basket fork → the new migration folds it.

## 5. Verify on real data

- [x] 5.1 Run against a COPY of `/Users/jeffreyh/Downloads/database.db` (the maintainer's Android export): packages 1 and 2 fold into one carrying 1020 shots.
- [x] 5.2 Confirm the grind step is unchanged at 0.25 from 28 settings — it already pooled both packages by model string, so this change must not move it.
- [x] 5.3 Run against a COPY of the dev Mac database (`~/Library/Application Support/DecentEspresso/Decenza/shots.db`), which has seven packages on one grinder and several retired lineages. Record what folds and what does not, and confirm nothing folds that shouldn't.
- [x] 5.4 Confirm `last_grind_setting` on the surviving package is the successor's, not the superseded one's.

## 6. Docs

- [x] 6.1 No wiki change — the heal is invisible to the user, who sees only that their grinder stopped looking split.
- [x] 6.2 If a shared `isEnrichmentOf()` lands, note in `equipmentstorage.cpp` that the heal and the live rule share it deliberately, and why (they drifted once).

## 7. Review and merge

- [x] 7.1 Full suite locally via `mcp__qtcreator__run_tests` (scope `all`) — nothing on GitHub builds a PR.
- [ ] 7.2 Open the PR, then run `/pr-review-toolkit:review-pr`.
- [ ] 7.3 Archive this change as the final commit on the same PR.
