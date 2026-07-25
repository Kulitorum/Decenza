## Context

`verify-recipe-editor-parity` established the plugins as the oracle and measured Decenza against them at four layers. The result is unusually well-bounded, so this design starts from what is already known rather than re-deriving it.

**Verified correct, and therefore not to be touched:**

| Layer | Evidence |
|---|---|
| Frame generation rules | All 8 A-Flow toggle combinations match `update_A-Flow` field for field; D-Flow's derived fill-pressure rule matches on all six branches including the floor |
| Editor parameter coverage | Both editors bind exactly their plugin's parameter set; the four Decenza-only parameters appear nowhere in QML |
| Legacy 6-frame upgrade | `Pre Fill` / `2nd Fill` / `Pause` inserted with the plugin's literals |
| BLE encoding | de1app's real `de1_packed_shot` as oracle: 8 stock profiles and 120 boundary-stressing generated profiles, **zero** quantisation differences; one positional byte differs (WIRE-1) |

**The defect is one layer:** what decides *which parameters* reach a correct generator.

Two facts about that layer, both confirmed in source:

- `Profile::toJsonObject()` (`src/profile/profile.cpp`) writes `obj["recipe"] = m_recipeParams.toJson()` whenever `editorType()` returns `dflow`/`aflow`, and `editorType()` derives from the profile **title**. A `.tcl` import carries no recipe by design; it is given one built from a default-constructed struct.
- `RecipeParams`'s member initialisers are live values, not sentinels — `targetWeight = 36.0`, `fillTemperature = 88.0`. So `ProfileManager::getOrConvertRecipeParams()`'s first branch, guarded by `recipeParams().targetWeight > 0`, fires on the fabricated block and never reaches the frame-derived path below it. 88 °C is what the matrix sees, on both editors.

**The architectural difference underneath all of it.** de1app's `update_*` procs mutate frames **in place**: read `::settings(advanced_shot)`, overwrite a named list of fields, write back — so any field not in that list survives untouched. Decenza builds frames from constants. Every field the plugin never writes is therefore a candidate divergence, which is what DF-1/DF-2/DF-5 and AF-6 are.

**The upstream answer is small and already read.** Both plugins reconstruct their whole editor state from the frames on every load, in a `proc prep` of a few dozen lines. There is no persisted high-level state anywhere in either plugin. `reference.md` in the parity change carries both procs transcribed with line citations.

## Goals / Non-Goals

**Goals:**

- Take the edit matrix from 13/99 to zero unexplained divergences.
- Make frame-derived extraction the single source of editor parameters for D-Flow and A-Flow, matching `prep`.
- Stop writing frame fields neither plugin's `update_*` writes.
- Close WIRE-1 so packed bytes are identical, not near-identical.
- Leave a user's existing saved profiles working, without rewriting values they set.

**Non-Goals:**

- Changing any file in `resources/profiles/`. They mirror upstream repos Decenza does not own. The fabricated blocks in the A-Flow built-ins are a symptom; the fix is to stop generating them, and the built-ins are re-synced from upstream separately if at all.
- Changing either plugin, or filing anything upstream.
- Retro-rewriting recipe blocks in users' saved profiles (see D5).
- Any new user-facing setting or migration prompt.
- Deciding `preserve-recipe-visualizer-roundtrip`. This change supplies what that decision needs; the decision itself is separate.

## Decisions

### D1 — Frames are the source of truth; a recipe block is a cache, never an oracle

Follow the plugins exactly: for a D-Flow or A-Flow profile, editor parameters come from the frames. A stored `recipe` block is at most a convenience and never overrides what the frames say.

*Alternative considered — keep the block authoritative and fix its contents.* Rejected. It preserves the failure mode: any profile arriving without a block (every `.tcl` import, every Visualizer download, every profile shared between apps) still needs frame derivation, so that path must be correct regardless. Two sources of truth means the wrong one eventually wins, which is precisely what happened.

*Consequence.* Once extraction is faithful, the block carries no information the frames lack. That is the finding that refutes `preserve-recipe-visualizer-roundtrip`'s premise, and it is why this change unblocks rather than competes with it.

### D2 — Write a recipe block only when the parameters were established

`toJsonObject()` gates on evidence, not on the title. Concretely: emit when the parameters came from a real extraction or a real edit; never from a default-constructed `RecipeParams`.

A `bool` "populated" flag on `RecipeParams` is the smaller mechanism, but it is a flag whose only job is to say "I am not the default", which is what a sentinel does more honestly. Prefer making the emission decision at the call site where the evidence exists — `Profile` knows whether its params were set — over threading state through the struct.

*Alternative considered — sentinel defaults (`NaN`, `-1`).* Rejected: `RecipeParams` is also used by the simple pressure/flow editors and by the MCP surface, where the defaults are load-bearing. Making them non-values ripples far beyond this change.

### D3 — Transcribe `prep`, do not improve on it

The A-Flow extraction becomes a direct transcription of `A_Flow/code.tcl`'s `prep`, with frame roles resolved through `set_profile_index`'s **positional** rule. No pattern matching, no name matching, no heuristics — those are exactly what `RecipeAnalyzer` does and why AF-1…AF-5 exist.

Where the plugin's rule looks odd, keep it. `fillTimeout` reading from `Pre Fill` (AF-5) is not being corrected to a "better" frame; the parameter is being removed (D4). Any place where transcription produces a value that looks wrong is a finding to record, not a licence to deviate.

*Alternative considered — one shared analyzer parameterised by editor type.* Rejected. The two plugins are different code with a shared lineage; a merged implementation is how a D-Flow analyzer came to be pointed at nine-frame profiles in the first place. Keep D-Flow's `prep` and A-Flow's `prep` as separate transcriptions, and let §6's inheritance tests catch the parts that must agree.

### D4 — Remove the four vestigial parameters rather than defining them

`fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled` have no plugin counterpart, appear in no QML, and `fillTimeout` writes `filling(seconds)` — a field neither `update_*` touches. §7 of the parity change recorded all four as defects rather than extensions, on the evidence of §8: nothing exposes them to a user.

*Alternative considered — keep them and stop writing frames from them.* Rejected as the worse half-measure: dead fields still serialise, still appear in the MCP surface, and still invite a future caller to set one.

*Migration note.* Their keys may exist in stored JSON. Reading and discarding an unknown key is already how `RecipeParams::fromJson` behaves for anything it does not recognise, so removal is read-compatible.

### D5 — Repairs are now-and-future; do not migrate stored user values

A user's saved profile may carry a fabricated block whose values they have since edited deliberately. There is no way to distinguish "88 °C because the struct said so" from "88 °C because I set it". Once extraction is frame-derived, a stored block that disagrees with its frames simply stops being consulted for the fields `prep` recovers — no rewrite pass, no migration.

The exception, if one is wanted at all, is App-invented values in profiles the user has never opened; that is a separate decision and not part of this change.

### D6 — The edit matrix is the gate, and it is regenerated, not edited

Acceptance is the matrix at zero unexplained divergences, re-run from `tools/gen_edit_matrix.py` against the pinned plugin commits. A golden is never hand-adjusted to match Decenza — if a golden looks wrong, the oracle is re-read, and if the oracle is right, Decenza changes.

Expected order, with predicted effect:

| Step | Findings | Predicted matrix |
|---|---|---|
| 1 | REC-1 | ~69 of 86 divergences clear, both editors |
| 2 | AF-1 … AF-5 (`prep`) | most of the remainder |
| 3 | AF-6, §7 (vestigial params) | the `Fill seconds` cluster |
| 4 | WIRE-1, DF-1/2/5 | frame-field and byte residue |

Each step lands with its expected-failures flipped to passes. A finding whose `QEXPECT_FAIL` is removed without its assertion becoming a real pass is a regression in the gate itself.

### D7 — Prefer a failure that is visible to one that is plausible

Carried forward from the parity change, because it is what made these findings findable. Where extraction cannot establish a parameter, the profile's frames are preserved as-is and the condition is reported — rather than substituting a default that produces a plausible-looking profile which brews something else. A wrong shot that looks right is the expensive failure here.

## Risks / Trade-offs

- **Users' saved profiles change appearance on next open** → They will show their own frames' values instead of struct defaults, which is the fix, but it will read as "my numbers changed" to anyone who never noticed the defaults were wrong. Mitigation: the frames — and therefore the shot — are what they always were; only the displayed parameters change, and they change to match. Worth a line in the release notes rather than a migration.

- **Removing four fields touches the MCP surface** → `profiles_get_params` / `profiles_edit_params` expose `RecipeParams`. Mitigation: none of the four is documented in `MCP_SERVER.md` as settable, and reading an absent key is already tolerated; verify against the MCP tool tests as part of step 3.

- **`prep` transcription could import a plugin bug** → By construction, yes. That is the contract: Decenza brews what de1app brews. Mitigation: divergences from *sensible* behaviour are recorded in `findings.md` with the plugin citation, so the choice is visible and can be revisited deliberately.

- **The matrix could reach 99/99 while something outside it is wrong** → It covers single edits from stock profiles. Compound edits, non-stock profiles, and the round-trip fixed point are separate assertions in the parity suite. Mitigation: keep those assertions; the matrix is the gate, not the whole suite.

- **WIRE-1 is a one-byte change in the BLE path** → Smallest fix in the change, largest blast radius if wrong. Mitigation: the wire test compares against de1app's real packer over 128 profiles; it either matches byte-for-byte or it does not.

## Open Questions

- ~~Does the D2 gate belong on `Profile` or as a `RecipeParams::isDefault()` predicate?~~ **Settled: on `Profile`,** as `hasRecipeParams()`, set by `setRecipeParams()` and by `fromJson` when a block is present. `isDefault()` was rejected on a concrete failure mode — a user who deliberately sets values equal to the defaults would have their block silently dropped. "Did anyone establish these?" is a fact about the profile's history, and no amount of inspecting the values can recover it. The flag also gave `regenerateFromRecipe()` the guard it needed to stop generating a whole plausible profile out of member initialisers.
- Should the five A-Flow built-ins be re-synced from the plugin once the fabrication stops, so their stale blocks disappear from the shipped files? That is a `resources/profiles/` edit and therefore outside this change's Non-Goals — raise it as a separate decision with the maintainer.
