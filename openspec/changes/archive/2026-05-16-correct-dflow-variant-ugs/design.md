## Context

`resources/ai/profile_knowledge.md` is parsed by `ShotSummarizer::loadProfileKnowledge()`: each `## ` heading becomes a `ProfileKnowledge` record with one `UGS:` value (leading `~` → `ugsInferred`, trailing parenthetical stripped), an `Also matches:` alias list, `AnalysisFlags:`, and free-text content. The section *title* is additionally split on `" / "` and each part registered as a key. Today one `## D-Flow` section enumerates seven variants via `Also matches:` under a single `UGS: 0.5 (…)`; the parenthetical is stripped, so all seven resolve to UGS 0.5 and canonical name "D-Flow".

The authoritative UGS chart, read verbatim from the calculator's JS source, contains exactly one D-Flow entry (base, 0.5) and "Londinium / LRv3" at 0. D-Flow/Q, Damian's Q, LRv2, LM Leva are absent — any value for them is inferred. The only production consumer of the parsed `ugs` (the grinder-calibration anchor path in `dialing_blocks.cpp`) already skips groups whose `canonicalName` differs from `canonicalNameForKbId(kbId)`; all cross-profile grind-transfer reasoning is the LLM reading the markdown tables, not code.

This is the strict subset of the `split-ugs-pressure-variants` proposal that fixes the reported bug with no speculative scaffolding.

## Goals / Non-Goals

**Goals:**

- Pressure-distinct D-Flow variants resolve to distinct UGS positions and distinct canonical names so the AI produces a directional grind adjustment instead of "no change."
- Base D-Flow (0.5) and LRv3 (== "Londinium / LRv3" == 0) are encoded exactly from the chart; the only inferred value is D-Flow/Q.
- Shared behavioral false-positive suppression is preserved for every variant.
- Zero production code change; regression test locks the content contract.

**Non-Goals:**

- No `Expected*:` structured-expectation schema, parser field, struct, cross-surface consumability work, system-prompt teaching clause, or deferred detector. All of that lives in `split-ugs-pressure-variants` and is explicitly excluded here.
- No change to the parser, the RGS anchor algorithm, or canonical/inferred `source` semantics.
- Not splitting all seven variants into seven sections — only along pressure/temperature fault lines.
- Not fixing the fuzzy-prefix matcher weakness for custom user-titled variant profiles; not touching the 80's Espresso UGS value or the pre-existing spec-example drift.

## Decisions

**D1 — Three sections, split on pressure/temperature, not seven sections.** The fault lines that matter: base ~9-bar/88°C; the 6-bar/84°C-fill Q; the pure ~0-UGS Londinium-R lever sims. La Pavoni and LM Leva stay with base (no chart entry, ~8–9 bar, behaviorally adjacent; inventing separate inferred positions would re-introduce the inferred-on-inferred fragility #1160 exists to remove). Behavioral grouping is preserved; only the quantitative UGS surface is split. *Alternative:* a single section with a pressure-offset annotation — rejected: the parser strips annotations, so it would need a parser change to have any effect.

**D2 — D-Flow/Q at `UGS: ~1.0`, inferred.** Not on the chart, so it must be `~`. Bounded below by base (0.5) and above by the chart's 6-bar profiles (Extractamundo/G&S at 1.5–2.0); the 6-bar approach pulls coarser while the low 84°C fill pulls slightly finer, so it lands ~0.5 coarser than base — unambiguously "noticeably coarser, don't transfer 1:1" without overclaiming. The regression test asserts only the ordering invariant (Q strictly coarser than base, LRv3 == 0), so the literal can be retuned from real history without breaking the spec.

**D3 — Q section title is `## D-Flow Q variant` (no `" / "`).** The parser splits titles on `" / "`; `## D-Flow / Q` would register the bare key `d-flow` and collide with the base section (last-parsed wins, order-fragile). Aliases `"D-Flow / Q"` / `"Damian's Q"` are carried via `Also matches:` (comma-split only, never `" / "`-split). `## Damian's LRv2 / LRv3` deliberately *does* use `" / "` — both parts are wanted keys and neither collides.

**D4 — Exact-alias match guarantees correct routing for built-ins.** `matchProfileKey` checks an exact normalized match before the fuzzy prefix loop; every built-in title is registered verbatim as an alias on the correct new section, so built-ins route correctly. The fuzzy-prefix weakness only affects custom titles that match no alias — pre-existing and out of scope.

**D5 — Re-anchor 80's Espresso off base D-Flow.** Its inferred-table rationale currently references "D-Flow / Q" — an inferred value anchored on what is now itself explicitly inferred. Re-anchor the rationale text to base D-Flow (canonical 0.5); the 80's Espresso UGS value is untouched.

**D6 — Mirror `docs/PROFILE_KNOWLEDGE_BASE.md`; review-only `docs/UNIVERSAL_GRIND_SETTING.md`.** The former is the human-facing twin with the same `## D-Flow` block and inferred table and must stay in sync. The latter is an ADR describing the approach, not the per-profile data; reviewed for stale "D-Flow covers 7 variants" prose, expected to need no substantive change.

## Risks / Trade-offs

- **[Inferred ~1.0 for Q is a judgment call]** → Marked `~` (flows to `source: "extrapolated"` in RGS, matching existing inferred semantics). The test asserts only the ordering invariant, so the literal is retunable without a spec/test break.
- **[Splitting changes `canonicalNameForKbId` for Q/LRv shots]** → The calibration anchor guard already excludes variant shots; after the split a Q shot's group canonicalName still differs from its anchor name, so behavior is unchanged or strictly more correct. Covered by existing `tst_dialing_blocks` calibration tests plus the new resolution assertions.
- **[Variant sections lose the umbrella's shared prose]** → Each variant section restates or cross-references the shared "DO NOT flag" notes; the base section remains the canonical home of shared mechanics. No false-positive suppression is lost.
- **[Two docs drift again]** → A grep parity check (resource vs. `PROFILE_KNOWLEDGE_BASE.md`: same D-Flow headings + inferred-table rows) is a task step.

## Migration Plan

Pure resource/text + test change. No code, no DB migration, no API surface, no rollout gating. Rollback is a single revert of the commit; parser and consumers are untouched, so there is no state to unwind. Verification is the existing Qt Test suite (must stay green) plus the new `tst_dialing_blocks` assertions, and a spot-check that the rendered `dialing_get_context` KB section and the in-app advisor prompt show the three new headings and corrected tables.

## Open Questions

- Should D-Flow/Q's inferred ~1.0 be retuned once enough real same-bean Q-vs-base shot pairs exist in user history? (Future tuning task, not a blocker — the contract is the ordering, not the literal.)
