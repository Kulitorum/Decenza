## Why

GitHub issue #1160 (parent #1147, reporter @fredphoesh): the Profile Knowledge Base collapses every D-Flow variant — base D-Flow, D-Flow/Q, Damian's Q, LRv2, LRv3, LM Leva, La Pavoni — into one `## D-Flow` section carrying a single `UGS: 0.5`. The parser strips the section's parenthetical, so the "LRv2/LRv3 at 0, Q at 0.5" nuance never reaches code. The AI therefore treats every variant as grind-equivalent and transfers a grinder anchor 1:1 across profiles with materially different pressure targets — D-Flow/Q runs a 6-bar approach, base D-Flow peaks ~9 bar. The authoritative UGS chart (videoblurb.com/UGS calculator source, read verbatim) has exactly one D-Flow entry (base, 0.5) and "Londinium / LRv3" at 0; D-Flow/Q, Damian's Q, LRv2, LM Leva are not on the chart at all. The chart's own two 6-bar profiles (Extractamundo 1.5, Gentle & Sweet 2) sit a full 1.0–1.5 UGS coarser than base D-Flow, confirming the mechanism: lower pressure target → coarser grind.

This is the **lean, focused correctness fix** for #1160 — a knowledge-base data correction that stops the AI giving a concretely wrong cross-profile grind recommendation. A broader "structured per-profile expected dial-in" capability is being explored separately in the `split-ugs-pressure-variants` change; that idea only pays off once a shot-summary deviation detector exists to consume it, so it is deliberately **not** part of this change. This change ships only what fixes the reported bug.

## What Changes

- Split the single `## D-Flow` section in `resources/ai/profile_knowledge.md` into three sections along pressure/temperature fault lines, keeping the shared behavioral guidance grouped:
  - `## D-Flow` — base ~9-bar / 88°C family. `UGS: 0.5` (canonical, confirmed by chart). Aliases: D-Flow / default, D-Flow / La Pavoni, Damian's D-Flow, Damian's LM Leva. Retains every shared "DO NOT flag" behavioral line and `AnalysisFlags: flow_trend_ok`.
  - `## D-Flow Q variant` — 6-bar approach, 84°C fill, medium-light. `UGS: ~1.0` (inferred — between base 0.5 and the chart's 6-bar profiles at 1.5; the low fill temp pulls slightly back finer). Aliases: "D-Flow / Q", "Damian's Q". Title deliberately omits `" / "` (the parser splits titles on it; `## D-Flow / Q` would register the key `d-flow` and collide with the base section) — aliases via `Also matches:`.
  - `## Damian's LRv2 / LRv3` — pure Londinium-R lever sims. `UGS: 0` (LRv3 == chart's canonical "Londinium / LRv3"); note LRv2 trends slightly coarser. Carries the lever-decline behavior plus the LRv2 flow-control-switch and LRv3 9-bar-hold specifics.
- Rewrite the two cross-profile ordering tables so the AI's textual reasoning matches the split: keep the canonical table's base D-Flow row at 0.5; replace the inferred-table row `~0–0.5 | Damian's LRv2, LRv3, LM Leva, Q | … treat as the same family.` (the single line that tells the AI they are grind-equivalent) with per-variant rows.
- Add an explicit "same named family ≠ same grind when the pressure target differs — never transfer a grinder anchor 1:1 across a pressure-target change" caveat to the "How to use this ordering" section, citing D-Flow ~9 bar vs D-Flow/Q 6 bar.
- Re-anchor the 80's Espresso inferred-table rationale off **base D-Flow** instead of off D-Flow/Q (currently inferred-anchored-on-inferred — the fragility #1160 calls out). The 80's Espresso UGS value is unchanged.
- Mirror the equivalent edits in `docs/PROFILE_KNOWLEDGE_BASE.md` (the `## D-Flow` block and the inferred-positions table) so the human-facing twin stays in sync.
- Add regression coverage in `tests/tst_dialing_blocks.cpp`: `ugsForKbId("d-flow / q")` strictly coarser than `ugsForKbId("d-flow / default")` and inferred; base D-Flow `0.5` canonical; Damian's LRv3 `0`; D-Flow/Q resolves to a canonical name distinct from base; Damian's Q resolves to the same position as D-Flow/Q; `flow_trend_ok` preserved on every variant.

No production code change. The `loadProfileKnowledge()` parser already supports multiple `## ` sections and the `~`-inferred convention. The only consumer of `ugsForKbId` (the grinder-calibration anchor path in `dialing_blocks.cpp`) already excludes variant profiles by canonical-name mismatch and continues to work correctly under the split. No new schema, no parser field, no system-prompt change, no deferred follow-up.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `dialing-context-payload`: the capability that owns "ProfileKnowledge SHALL expose UGS as a parsed numeric field" and the relative-grinder-setting / anchor-selection requirements. One ADDED requirement: the Profile Knowledge Base SHALL assign distinct UGS positions to profile variants whose pressure targets differ materially, and SHALL NOT encode one UGS for pressure-distinct same-family variants. The UGS parser behavior, the RGS anchor-selection algorithm, and the canonical/inferred `source` semantics are unchanged — only the data contract the KB must satisfy is tightened, with a regression scenario.

## Impact

- **Data / resources**: `resources/ai/profile_knowledge.md` — the `## D-Flow` section split into three; both cross-profile tables; the "How to use" caveat; the 80's Espresso rationale re-anchor. Shipped via Qt resource into both AI paths (in-app advisor system prompt and `dialing_get_context`); the corrected prose/tables are consumed by both with no plumbing change.
- **Docs**: `docs/PROFILE_KNOWLEDGE_BASE.md` mirrored. `docs/UNIVERSAL_GRIND_SETTING.md` reviewed; no change expected (it is an ADR, not the data file).
- **Tests**: new assertions in `tests/tst_dialing_blocks.cpp`. No DB migration, no schema/storage change, no API change, no code change.
- **Behavioral**: the AI sees D-Flow/Q as coarser than base D-Flow and LRv3 as finer, so cross-profile grind transfer within the "D-Flow family" produces a directional adjustment instead of "no change." Behavioral false-positive suppression (declining pressure, soak, flow-switch) is preserved because the shared guidance stays in the base section and is cross-referenced from the variant sections.
- **Relationship to `split-ugs-pressure-variants`**: that change is the broader exploratory proposal (general `Expected*:` schema + deferred detector). This change is the strict subset that fixes the reported bug with no speculative scaffolding. Exactly one of the two should ultimately be applied; this one is the low-risk default.
- **Closes**: tracked under #1147; #1160 stays open with a plain reference (cc @fredphoesh) until the reporter confirms — not auto-closed.
- **Out of scope**: the `Expected*:` structured-expectation schema, parser field, cross-surface consumability, directional-reading teaching clause, and the deferred deviation detector (all in `split-ugs-pressure-variants`); reconciling the pre-existing drift between the spec's 80's Espresso example value (`canonical UGS 0.25`) and the KB's current `~-0.5`; the fuzzy-prefix matcher weakness for custom user-titled variant profiles (pre-existing; built-in profiles match exactly); the remaining #1147 sub-issues (#1159).
