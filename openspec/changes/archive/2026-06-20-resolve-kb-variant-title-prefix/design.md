## Context

`ShotSummarizer::matchProfileKey(title, editorHint)` (in `src/ai/shotsummarizer_kb.cpp`) resolves: (1) `normalizeProfileKey(title)` → exact lookup in `s_aliasToId` (built from each entry's `displayName` + every `alsoMatches` + the synthetic `__editor_default__:<type>` key); (2) editor-type default fallback. `resolveKbInput(kbId)` resolves a persisted kbId: id-passthrough, else exact alias→id, else `""`. The #1192 restructure deleted the prior order-dependent greedy `startsWith`/`contains` scan (it silently mis-resolved real profiles). The KB JSON registers recipe aliases `D-Flow / Q` and `Damian's Q` → `d-flow-q-variant`, `D-Flow / La Pavoni` → `d-flow-la-pavoni-variant`; `d-flow` and `a-flow` are `defaultForEditorType` entries (the editor namespace, not recipes). Renamed variants (`D-Flow / Q - Jeff`, `D-Flow / Q2`) miss the exact alias and degrade to the generic `d-flow` default.

## Goals / Non-Goals

**Goals:**
- `D-Flow / Q - <anything>`, `D-Flow / Q2`, `D-Flow / Q3`, `D-Flow / Q-Jeff`, `Damian's Q - decaf` (and the A-Flow analogue) resolve to `d-flow-q-variant`; `D-Flow / La Pavoni <suffix>` to `d-flow-la-pavoni-variant`.
- No regression to exact-match resolution; no order-dependent / non-anchored / non-deterministic best-guess reintroduced.
- Legacy persisted variant-title kbIds heal via the same shared resolver (recompute-on-load).

**Non-Goals:**
- No `contains`/substring matching (`"Jeff's D-Flow / Q"` staying generic is acceptable — every acceptance case is prefix-shaped).
- No KB content/schema/parser change; no new directive; no DB migration.
- No new hard validator rejection rule (see D5).

## Decisions

**D1 — Longest recipe-alias boundary-prefix wins; the issue's literal "reject if >1 distinct id matches" is rejected.** `d-flow / q - jeff` boundary-prefixes both `d-flow` (→ `d-flow`) and `d-flow / q` (→ `d-flow-q-variant`) — reject-on-multiple would reject the *primary target*. Longest-prefix is the correct, total disambiguation (the more specific recipe alias wins). *Why safe:* a string has exactly one prefix of each length, so two equal-length boundary-prefix aliases must be the identical string ⇒ the only true tie reduces to the validator's existing alias-collision rejection. No new ambiguity class is introduced.

**D2 — Editor-name aliases (`defaultForEditorType` entries) are excluded from prefix anchoring.** Editors are namespaces, not recipe identities (the KB author's own `ugs.note`: *"D-Flow is the editor, not a profile"*). The editor's "which editor" job is *already and only* the explicit step-3 `defaultForEditorType` fallback. Anchoring on the bare `D-Flow` alias would prefix every `D-Flow / *` regardless of recipe — precisely the arbitrary-substring greedy behavior #1192 deleted. Only recipe-bearing aliases anchor. Consequence: an unrecognized recipe (`D-Flow / Bradbury` + dflow hint) still reaches generic `d-flow` — via step 3, the correct home for "know the editor, not the recipe."

**D3 — Separator set is the closed enumeration { `/`, `-`, space, ASCII digit }, not "any non-letter".** Closed = deterministic and minimal. Digit is required for `Q2`/`Q3`; `-` for `Q-Jeff`; space/`/` for `/ Q - Jeff`. A trailing letter blocks the match (`d-flow / quark` — char after `d-flow / q` is `u` — does NOT resolve to the Q variant; `D-FlowX` does not prefix-match `d-flow`). End-of-string is not a separator (that is the exact-match case, already handled by step 1).

**D4 — Prefix only, never `contains`/substring.** The unbounded substring scan was the deleted footgun. Prefix-anchored-on-a-registered-recipe-alias is bounded and deterministic.

**D5 — No new hard validator rule; ship one non-fatal informational lint (decided, evidence-backed).** Correctness follows from D1 (no new ambiguity class) — the existing alias-collision rejection already covers the only true tie. Add a WARN-level lint when one recipe alias boundary-prefixes another mapping to a different id (legitimate specificity ordering; surfaced so authors aren't surprised), consistent with the existing D9 best-effort-lint precedent. *Evidence:* simulated against the shipped KB — **0 hits across all 130 recipe-alias anchors** (the existing validator already emits 10 non-fatal WARNs, so this adds zero noise and is pure future-proofing for the one authoring footgun). The C++ resolution test (positive + negative + relational fixtures) remains the real correctness gate, consistent with the spec's existing "corpus resolution test SHALL be a hard gate" requirement. The earlier open question is resolved: ship the lint.

**D6 — One shared recipe-prefix helper for `matchProfileKey` and `resolveKbInput`.** User decision: legacy persisted variant-title kbIds on old shot records heal retroactively under recompute-on-load. One code path, no stored-data rewrite, no migration — pure resolution.

**D9 — The rule is profile-general, not D-Flow/A-Flow-specific.** Every documented profile's aliases anchor (130 measured); only the two `defaultForEditorType` editor entries are excluded (D2). D-Flow/Q is the issue's motivating case, not the scope: simulation confirms `Adaptive v2 - Jeff` → `adaptive-v2`, `Londinium - Jeff` → `londinium`, `Allongé - decaf` → `allonge`, `Blooming Espresso - test` → `blooming-espresso`, `Cremina - morning` → `cremina-lever-machine`, `80's Espresso - Jeff` → `80s-espresso` all via the same step. Examples and test fixtures SHALL include non-D-Flow profiles so the generality is enforced, not incidental. *Non-goal note:* a title in kebab-id form with a suffix (`adaptive-v2 - jeff`) does not prefix-match (the registered alias is the space-normalized displayName `adaptive v2`); this is not a real input shape — canonical ids resolve via `resolveKbInput` id-passthrough, and user/profile titles use the displayName form — so it is deliberately not special-cased (no handling for an input that cannot occur).

**D8 — Built-in profiles SHALL resolve by exact match; the prefix step is exclusively the user-derived-profile path (user-stated policy).** Every shipped/built-in/starter profile and editor-canonical output is a registered exact alias (already gated by the spec's existing "corpus resolution test SHALL be a hard gate" requirement — it asserts each resolves to exactly one id). The recipe-prefix step is reached *only after* an exact miss, so it can never override or alter a built-in's resolution; it is the best-effort safety net for *user-created profiles derived from a real recipe* (the kept-prefix convention). A test invariant pins this: every built-in title resolves at step 1 and does not depend on the prefix step. This also drives a user-facing documentation obligation (the Manual "save a profile" page) so the kept-prefix convention is discoverable, not hidden behavior.

**D7 — The pre-#1198 dialing_blocks test is inverted, not weakened.** `calibrationBlock_unresolvedCustomTitleEmittedAsRawHistoryRow` currently asserts `D-Flow / Q - Jeff` stays an unresolved raw history row and does *not* aggregate into `d-flow-q-variant`. The issue explicitly calls for this re-evaluation ("they'd then collapse into the parent id group automatically"). The inverted assertions are *stronger*: verify the variant resolves and its shots correctly contribute history to the parent group (correct aggregation, not merely non-contamination).

## Risks / Trade-offs

- **[Prefix rule resurrects the deleted greedy scan]** → D2/D3/D4/D1: anchored on registered recipe aliases only, closed separator set, prefix-only, longest-wins total order — deterministic and test-gated; structurally not the order-dependent best-guess. The spec requirement text is amended to state this distinction precisely.
- **[A genuinely distinct recipe is absorbed by a shorter parent alias]** → longest-wins + registered-alias anchor: the moment that recipe gets its own alias/entry it wins automatically; until then it inherits the closest *documented* parent — strictly better than today's generic default. Negative fixtures pin the letter-boundary.
- **[`resolveKbInput` blast radius]** → D6 is the user's explicit choice; shared helper = one path; recompute-on-load means no stored data changes (pure resolution); covered by tests.
- **[O(1)→O(n) per resolution]** → ~130 recipe-alias anchors (measured against the shipped KB, larger than first estimated); resolution is per-shot-analysis / per-dial-in-context, not per-telemetry-tick — still negligible, but the optional micro-opt (pre-sort recipe aliases by descending length at load, first boundary hit wins) is mildly more worthwhile at this count. Not a blocker.
- **[Inverted test reads as a weakened gate]** → D7: a sanctioned, issue-anticipated contract change; the new assertions verify correct aggregation and are stronger than the old non-contamination check.
- **[A built-in profile silently starts depending on the prefix step]** → D8: exact match is step 1 and always wins for registered built-ins; a test invariant asserts every shipped/starter title resolves at step 1 independent of the prefix step, so a future KB edit that drops a built-in's exact alias fails loudly rather than masking via prefix.
- **[Kept-prefix convention is invisible to users]** → D8 documentation task: the Manual "save a profile" page states the convention explicitly; without it users who fully rename a derived profile silently lose recipe knowledge with no signal.

## Migration Plan

Pure code + test. No DB/schema/parser/KB-data change. Build + the dialing/summarizer Qt Test suites via Qt Creator MCP (not CLI). Rollback = single revert (variants revert to generic editor default).

## Open Questions

- None. The validator-scope question is resolved (D5): the lint is simulated to fire 0 times on the shipped KB, so it ships as zero-noise future-proofing.
