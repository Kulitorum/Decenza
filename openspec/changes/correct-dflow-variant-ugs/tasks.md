## 1. Pre-flight audit

- [ ] 1.1 Re-confirm the exact built-in profile titles by grepping `resources/profiles/*.json` for the `title` field of: `d_flow_default.json`, `d_flow_q.json`, `d_flow_la_pavoni.json`, `damian_s_q.json`, `damian_s_lm_leva.json`, `damian_s_lrv2.json`, `damian_s_lrv3.json`. Record the verbatim titles for use as `Also matches:` aliases.
- [ ] 1.2 Capture baseline counts for the drift check: number of `## ` headings in `resources/ai/profile_knowledge.md`, and the current `## D-Flow` section span. Do the same for `docs/PROFILE_KNOWLEDGE_BASE.md`.
- [ ] 1.3 Grep all section titles and `Also matches:` lines in `profile_knowledge.md` for the keys the new sections will register (`d-flow`, `d-flow / q`, `damian's q`, `damian's lrv2`, `damian's lrv3`, `damian's d-flow`, `damian's lm leva`, `d-flow / la pavoni`) to confirm no other section already claims them and that `## Londinium` still owns `londonium` / `londinium / lrv3` (must NOT be re-registered by the new LRv2/LRv3 section).

## 2. Split the D-Flow section in `resources/ai/profile_knowledge.md`

- [ ] 2.1 Replace the single `## D-Flow` section with `## D-Flow` (base): `UGS: 0.5` (no parenthetical needed now); `Also matches:` carrying "D-Flow / default", "D-Flow / La Pavoni", "Damian's D-Flow", "Damian's LM Leva"; `AnalysisFlags: flow_trend_ok`; keep the shared "How it works / Expected curves / Grind / Roast" prose and every "DO NOT flag" behavioral line. Keep the LM Leva and La Pavoni descriptive paragraphs here.
- [ ] 2.2 Add `## D-Flow Q variant` — title MUST NOT contain `" / "`. `UGS: ~1.0 (inferred — 6-bar approach pulls coarser than base D-Flow; 84°C fill pulls slightly back finer; not on the UGS chart)`. `Also matches: "D-Flow / Q", "Damian's Q"`. `AnalysisFlags: flow_trend_ok`. Content: the 6-bar / 84°C-fill / medium-light specifics (migrated from the old umbrella's Damian's Q paragraph and the temperature paragraph), plus a one-line cross-reference to the shared D-Flow behavioral guidance so no "DO NOT flag" suppression is lost.
- [ ] 2.3 Add `## Damian's LRv2 / LRv3` (the `" / "` split into `damian's lrv2` and `damian's lrv3` keys is intended here and collides with nothing). `UGS: 0 (Londinium / LRv3 chart position; LRv2 trends slightly coarser than LRv3)`. `AnalysisFlags: flow_trend_ok`. Content: the pure-Londinium-R lever-sim prose, the LRv2 flow-control-switch behavior, the LRv3 9-bar-hold behavior, the LRv2/LRv3 temperature note, plus a cross-reference to shared D-Flow behavioral guidance. Do NOT add `Londonium` / `Londinium / LRv3` aliases (owned by `## Londinium`).
- [ ] 2.4 Verify no `## ` heading count regression except the intended `+2`, and that every one of the seven built-in titles still resolves to exactly one section via an exact alias.

## 3. Cross-profile tables and guidance in `resources/ai/profile_knowledge.md`

- [ ] 3.1 In the canonical UGS table, keep the base `D-Flow | 0.5` row; confirm the `Londinium / LRv3 | 0` row is present and accurate against the chart. No fabricated chart rows for Q (it is not on the chart).
- [ ] 3.2 In the inferred-positions table, replace the row `~0–0.5 | Damian's LRv2, LRv3, LM Leva, Q | Londinium / D-Flow adjacent; treat as the same family.` with per-variant rows: D-Flow/Q (Damian's Q) at ~1.0 with the 6-bar/coarser rationale and an explicit "do NOT transfer a base-D-Flow grind anchor 1:1" note; LRv2/LRv3 at ~0 (LRv3 == canonical Londinium/LRv3, LRv2 slightly coarser); LM Leva at ~0.5 (≈ base D-Flow, ~8-bar).
- [ ] 3.3 Re-anchor the 80's Espresso inferred-table rationale to reference **base D-Flow** instead of "D-Flow / Q". Do NOT change the 80's Espresso UGS value.
- [ ] 3.4 In "How to use this ordering", add an explicit caveat: profiles in the same named family but with different pressure targets are NOT grind-equivalent; never transfer a grinder anchor 1:1 across a pressure-target change — cite D-Flow ~9 bar vs D-Flow/Q 6 bar by name.

## 4. Documentation sync

- [ ] 4.1 Apply the equivalent `## D-Flow` split and inferred-table rewrite to `docs/PROFILE_KNOWLEDGE_BASE.md` (the human-facing twin), including its `Also matches:` example line and the `~0–0.5 | Damian's LRv2, LRv3, LM Leva, Q` row. Keep its parser-gotcha note about `" / "` title splitting consistent with the new `## D-Flow Q variant` naming.
- [ ] 4.2 Review `docs/UNIVERSAL_GRIND_SETTING.md` for any stale factual claim that contradicts the split (e.g. "D-Flow covers 7 Damian variants" coverage prose). Update only stale claims; the ADR's approach narrative stays. Record in the task notes if no change is needed.
- [ ] 4.3 Grep-verify the resource and `PROFILE_KNOWLEDGE_BASE.md` agree: same set of D-Flow `## ` headings and the same inferred-table variant rows in both files.

## 5. Regression tests

- [ ] 5.1 In `tests/tst_dialing_blocks.cpp`, add a test that loads the real KB and asserts: `ugsForKbId(computeProfileKbId("D-Flow / default","dflow"))` == 0.5 and not inferred; `ugsForKbId` for "D-Flow / Q" strictly greater than for "D-Flow / default" and inferred; `canonicalNameForKbId` for Q differs from base; "Damian's Q" matches the Q canonical name and UGS; "Damian's LRv3" UGS == 0 and strictly less than base.
- [ ] 5.2 Add an assertion that `ShotSummarizer::getAnalysisFlags(kbId)` contains `flow_trend_ok` for the kbIds of "D-Flow / default", "D-Flow / Q", and "Damian's LRv2" (shared behavioral suppression preserved).
- [ ] 5.3 Confirm the existing `tst_dialing_blocks` grinder-calibration tests that reference `"D-Flow / Q"` and `profileKbId = "d-flow"` still pass under the split (variant guard still excludes Q from anchoring); adjust only if an assertion encoded the old single-canonical-name assumption.

## 6. Build and verification

- [ ] 6.1 Build via the Qt Creator MCP (list_projects → match the worktree path → build) per project policy. Resource + test only; expect a clean build.
- [ ] 6.2 Run the full Qt Test suite via the Qt Creator MCP. Zero failures; the new `tst_dialing_blocks` cases green; no regression in `tst_dialing_blocks` / `tst_shotsummarizer`.
- [ ] 6.3 Spot-check the rendered KB on both surfaces: read the `dialing_get_context` KB section and the in-app advisor system prompt and confirm the three new headings, the corrected inferred table, the new "How to use" caveat, and the base-D-Flow-anchored 80's Espresso rationale all appear as intended.

## 7. PR and archive

- [ ] 7.1 Run `/review` on the branch before opening the PR.
- [ ] 7.2 Open the PR with `gh pr create` targeting `main`. Reference #1160 and #1147 with plain links (NOT `Closes #`), cc @fredphoesh; note it is a #1147 sub-issue awaiting reporter confirmation, and that it is the lean correctness fix (the broader `Expected*:` exploration lives in the separate `split-ugs-pressure-variants` proposal, deliberately not applied).
- [ ] 7.3 After merge (squash + delete branch per project standard), archive this OpenSpec change with `openspec archive correct-dflow-variant-ugs`.
