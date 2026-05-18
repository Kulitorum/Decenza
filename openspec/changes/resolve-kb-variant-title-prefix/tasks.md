> Deterministic recipe-alias longest-boundary-prefix resolution between exact-match and editor-default. Editors are namespaces (excluded as anchors); recipe aliases anchor; longest wins; prefix-only; closed separator set. Shared by `matchProfileKey` + `resolveKbInput`. No KB data/schema/parser change. Build + tests via Qt Creator MCP, not CLI.

## 1. Resolver

- [x] 1.1 Add a shared helper in `src/ai/shotsummarizer_kb.cpp` (e.g. `recipePrefixResolve(const QString& normalizedKey)`): over registered aliases that do **not** belong to a `defaultForEditorType` entry (recipe aliases only — D2), return the `id` of the **longest** alias that is a *boundary-prefix* of `normalizedKey` (alias immediately followed by a separator ∈ { `/`, `-`, space, ASCII digit } — D3); empty if none (D1, D4). Caller passes an already-normalized key (same `normalizeProfileKey`).
- [x] 1.2 Track which aliases are recipe aliases at load (in `loadProfileKnowledge`): the synthetic `__editor_default__:<type>` key and the `displayName`/`alsoMatches` of any entry with `defaultForEditorType` set are NOT recipe-prefix anchors. (Either a parallel recipe-alias structure or a per-id "is editor default" flag — implementer's call; keep `s_aliasToId` semantics unchanged.)
- [x] 1.3 `matchProfileKey`: insert the recipe-prefix step **after** the exact `s_aliasToId` lookup and **before** the editor-type default fallback. Exact match still wins first; editor default still last; both unchanged.
- [x] 1.4 `resolveKbInput`: after id-passthrough and exact alias→id, route the normalized input through the same shared helper before returning `""` (D6 — legacy persisted variant titles heal under recompute-on-load).
- [x] 1.5 (Optional micro-opt) pre-sort recipe aliases by descending normalized length once at load; first boundary hit is the longest.

## 2. Validator

- [x] 2.1 `tools/validate_kb.py`: add a **non-fatal WARN** when one recipe alias (alias of a non-`defaultForEditorType` entry) is a boundary-prefix of another recipe alias mapping to a different `id` (D5 — author awareness of specificity ordering; not a hard rule). Reuse the existing `norm()`; keep it consistent with the C++ separator definition. No new ERROR-level rule.

## 3. Tests

- [x] 3.1 Positive fixtures (`tests/tst_dialing_blocks.cpp`, alongside the existing D-Flow/Q resolution cluster): (a) D-Flow cluster — `D-Flow / Q - Jeff`, `D-Flow / Q - <bean>`, `D-Flow / Q2`, `D-Flow / Q3`, `D-Flow / Q-Jeff`, `Damian's Q - decaf` → `d-flow-q-variant`; `D-Flow / La Pavoni 80s` → `d-flow-la-pavoni-variant` and strictly coarser UGS than `D-Flow / default`. (b) **Generality (non-D-Flow) — required, not optional**: `Adaptive v2 - Jeff` → `adaptive-v2`, `Londinium - Jeff` → `londinium`, `Allongé - decaf` → `allonge` (D9 — prove the step is profile-general and not editor-cluster-scoped).
- [x] 3.2 Negative fixtures: `D-Flow / Quark` and `D-FlowX` do NOT resolve to `d-flow-q-variant` (letter blocks the boundary — D3); fully-custom `My Morning Pull` with `dflow` hint still → generic editor default (`d-flow`), and with no hint → unresolved (editor name is not an anchor — D2); exact `D-Flow / Q` and `Damian's Q` unchanged.
- [x] 3.2a Built-in-exact-match invariant (D8): every shipped/built-in/starter profile title and editor-canonical output resolves via the step-1 exact alias lookup and does NOT depend on the recipe-prefix step (e.g. assert resolution is identical with the prefix step disabled, or assert each built-in title is a registered exact alias). Reuse/extend the existing corpus resolution gate rather than duplicating it.
- [x] 3.3 `resolveKbInput` fixture: a legacy persisted kbId `"d-flow / q - jeff"` resolves to `d-flow-q-variant` via the shared helper (D6).
- [x] 3.4 **Invert** `calibrationBlock_unresolvedCustomTitleEmittedAsRawHistoryRow` (D7): the 4 `D-Flow / Q - Jeff` shots now resolve to `d-flow-q-variant`; assert they aggregate into that group's history (`source == "history"` for the variant, RGS reflecting those shots) and there is no separate raw `D-Flow / Q - Jeff` row. Rename/redocument the test to reflect the post-#1198 contract.

## 4. Documentation

- [ ] 4.0 Update the "save a profile" section of the user Manual wiki (separate repo — clone `Kulitorum/Decenza.wiki.git`; page `Manual`, `https://github.com/Kulitorum/Decenza/wiki/Manual`): explain that built-in profiles are always recognized exactly, and a profile created from a real recipe inherits that recipe's AI dialing knowledge **only if its title keeps the recipe's name as the prefix** (e.g. `D-Flow / Q - My Ethiopian`, `D-Flow / Q2`); a fully-renamed custom profile falls back to the generic editor default. Keep wording aligned with the actual separator/boundary behavior (D2/D3). Commit in the wiki repo after the code lands (document shipped behavior, not aspirational).

## 5. Verification

- [x] 5.1 `openspec validate resolve-kb-variant-title-prefix --strict` → valid.
- [x] 5.2 `python3 tools/validate_kb.py` → still OK; the new nested-recipe-alias lint adds **0** WARNs on the current KB (simulated), the existing 10 non-fatal WARNs are unchanged, zero ERRORs, KB data unchanged.
- [x] 5.3 Built via Qt Creator MCP (0 errors, 0 warnings); full Qt Test suite green (no WARN lines), with the new + inverted resolution fixtures passing.
- [ ] 5.4 Live spot-check via `dialing_get_context` on a real renamed-variant shot: `profileKnowledge` resolves to `d-flow-q-variant` (6-bar / 84°C / UGS ~1.0), not the generic `d-flow` default; previously `no kbId resolvable` variants now resolve.
