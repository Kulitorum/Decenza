# Tasks

The edit matrix (`editMatrixMatchesDe1app`, 99 cases) is the gate throughout. Record its
score after each numbered section — a step that does not move it did not do what it claimed.
Starting point: **13/99 rows, 317 differing fields**. Both numbers matter — the row
count hides partial progress, since a row counts once whether one field differs or five.

  after §1 (REC-1)      75 rows / 317 fields — all 24 D-Flow rows clear
  after §2 (prep)       75 rows /  75 fields — exactly one field left per row
  after §3 (vestigial)   0 rows /   0 fields — TARGET MET
  §5 re-verified        regenerated from the pinned plugin commits, data rows
                        byte-identical; compound (two successive saves) also 8/8

Never adjust a golden to match Decenza. If a golden looks wrong, re-read the oracle; if the
oracle is right, Decenza changes (design D6).

## 1. REC-1 — stop fabricating recipe blocks

- [x] 1.1 Settle the D2 open question: gate the recipe-block write on `Profile` knowing its params were established, or on a `RecipeParams::isDefault()` predicate. Decide with the call sites in front of you and record which and why in `design.md`.
- [x] 1.2 Gate `Profile::toJsonObject()`'s recipe-block emission on that evidence rather than on `editorType()` matching the title. A default-constructed `RecipeParams` must produce no block.
- [x] 1.3 Make `ProfileManager::getOrConvertRecipeParams()` reach the frame-derived path whenever no genuine block exists. Its current guard (`targetWeight > 0`) is satisfied by the struct default of 36.0 and must not be the test.
- [x] 1.4 Check every other producer of a recipe block — importer, converter, `ProfileSaveHelper`, the MCP save/edit tools — for the same title-implies-block assumption.
- [x] 1.5 Flip the REC-1 expected-failures to real passes. Re-run the matrix; expect ~69 of the 86 divergences to clear, on **both** editors.

## 2. AF-1…AF-5 — implement `prep`

- [x] 2.1 Transcribe `A_Flow/code.tcl`'s `prep` as A-Flow's extraction, replacing the D-Flow pattern analyzer for that editor. Cite the plugin line for each rule, as `reference.md` does. Keep D-Flow's extraction separate (design D3).
- [x] 2.2 Resolve frame roles through `set_profile_index`'s positional rule, covering the 9-frame and legacy 6-frame layouts. No name matching, no sequence pattern matching.
- [x] 2.3 AF-1: pour flow from the `Flow Start` frame. This is the error that compounds per save (2× each round-trip), so verify the round-trip fixed-point assertions go green with it.
- [x] 2.4 AF-3: ramp time as the **sum** of the ramp-up and ramp-down frame durations, including the odd-value remainder the plugin puts on the decline frame.
- [x] 2.5 AF-2 / AF-4: derive all three toggles from frame structure — `ramp_down(seconds) > 0`, `pouring(flow) > pouring_start(flow)`, and the pause frame's non-zero duration on a 9-frame layout. `A-Flow / default-very-dark` must extract ramp-down **enabled**, per its frames and the plugin readme.
- [x] 2.6 Flip the AF-1…AF-5 expected-failures. Re-run the matrix; expect most of the remaining divergences to clear.

## 3. AF-6 and §7 — remove the vestigial parameters

- [x] 3.1 Remove `fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled` from `RecipeParams` and every use. Confirm first that none has gained a QML or MCP surface since §8 established it had none. — **§8's verdict was QML-only: all four DO have an MCP surface**, in `profiles_get_params`' output and `profiles_edit_params`' schema. So they were reachable by an AI, not invisible. Removed anyway — a knob that writes a frame field the plugin preserves breaks parity whoever turns it. Recorded as a correction to §7's evidence.
- [x] 3.2 Stop writing `filling(seconds)` from the removed `fillTimeout` — neither plugin's `update_*` writes that field.
- [x] 3.3 Verify `RecipeParams::fromJson` still reads a stored profile carrying the removed keys, discarding them as it discards any unknown key. Add a test with such a profile.
- [x] 3.4 Check the MCP surface (`profiles_get_params`, `profiles_edit_params`) and its tests for references to the removed fields.
- [x] 3.5 Flip the AF-6 and §7 expected-failures. Re-run the matrix; expect the `Fill seconds` cluster to clear. — **matrix is at 0/99: zero divergences, zero differing fields.** Removing the four was not sufficient on its own; the generator still had to stop inventing the fields they used to carry. That is `Profile::restoreFieldsThePluginNeverWrites` (4.1's mechanism, pulled forward — see §4).

## 4. DF-1 / DF-2 / DF-5 and WIRE-1 — the residue

4.1's mechanism landed early, in §3: removing the four vestigial parameters left the
generator inventing the frame fields they used to carry, so the plugins' in-place-mutation
semantics had to be reinstated for the matrix to clear. `restoreFieldsThePluginNeverWrites`
restores, by frame ROLE, every field the corresponding `update_*` proc never assigns.
What remains here is the generator-level assertions and WIRE-1.

- [x] 4.1 Preserve `filling(volume)`, `filling(weight)` and `pouring(volume)` from the source profile rather than generating them from constants. DF-5 is the one that changes a shot: forcing `pouring(volume)` to 0 removes a stop cap, because the firmware reads `MaxVol 0` as "ignore". — landed in §3 as `restoreFieldsThePluginNeverWrites`. Note the constants themselves were never wrong: 100 / 5.0 / 0 are `D-Flow / default`'s own values, right for a from-scratch profile and wrong for every existing one.
- [x] 4.2 Check whether the issue #331 passthrough restore in `regenerateFromRecipe` becomes redundant once generation stops overwriting these fields. If it does, remove it rather than leaving two mechanisms for one job; if it does not, record what it still covers. — **replaced, not kept alongside.** #331 restored volume and exitWeight by frame-NAME match; the general rule restores by frame ROLE every field the matching `update_*` never assigns, which strictly contains it and also covers `soaking(exit_pressure_over)` (DF-4) and the fill frame's seconds/pressure/flow that #331 let through.
- [x] 4.3 DF-4: stop rewriting `soaking(exit_pressure_over)`. Confirmed inert at runtime (`exit_if 0`), so this is tidiness, not a shot fix — do it with 4.1 or not at all. — covered by the same role-based rule.
- [x] 4.4 WIRE-1: send `0x00` for the tail's `MaxTotalVolume` marker byte, matching de1app. Re-run the wire test over the 8 stock profiles and the 120-profile boundary corpus; every byte must match. — done, and the WIRE-1 filters are **deleted** from all three wire tests rather than left tolerating nothing. All 89 de1app profiles + 120 boundary profiles now match byte for byte with nothing excluded. Worth recording: this was never cosmetic padding — de1app's own comment on that field is "Unused. Use highest bit to enable / disable preinfusion tracking", so Decenza was asserting a firmware flag on every profile that de1app never sets.
- [x] 4.5 Flip the DF-1/2/5 and WIRE-1 expected-failures. — **zero `QEXPECT_FAIL` remain in `tst_recipeeditorparity`**, and the two left in `tst_recipeeditorapppath` are the per-row matrix marker and a comment. DF-3 is the one divergence still allowed, by name, with La Pavoni asserted as an exact fixed point so the allowance cannot hide a drifting derived rule.

## 5. Gate and regression

- [x] 5.1 Regenerate the edit matrix from `tools/gen_edit_matrix.py` against the pinned plugin commits and record the final score. Any remaining divergence is named and justified in `findings.md`, or it is a defect. — regenerated against A_Flow `e1a4d871` / D_Flow `7f3c9726`; **every data row came back byte-identical**, so no golden was ever hand-adjusted. **0 divergences, 0 differing fields, 99 cases.**
- [x] 5.2 Assert the round-trip fixed point holds for all five stock A-Flow profiles and all three stock D-Flow profiles — load, save unedited, no frame field changes. — holds for all eight, through both `regenerateFromRecipe` and the `ProfileManager` path. The one moving field is D-Flow's `filling(exit_pressure_over)`, which `update_D-Flow` genuinely derives (DF-3) and de1app rewrites identically on first edit; La Pavoni is asserted as an exact fixed point so that allowance cannot mask a drifting rule.
- [x] 5.3 Assert a compound edit — two parameters changed in sequence — matches the plugin, so the matrix's single-edit coverage is not the only evidence. — `compoundEditMatchesDe1app`, 8 profiles. The oracle now takes N `<global> <value>` pairs and runs a full `prep` → `update` cycle per pair, so de1app re-derives from the frames the previous save wrote — the exact shape in which AF-1 compounded. A-Flow edits pourFlow then rampDownEnabled; D-Flow edits infusePressure then pourTemperature. All 8 match.
- [x] 5.4 Assert no expected-failure was removed without its assertion becoming a real pass. A finding id that no longer appears anywhere in the suite is a hole in the gate. — `everyFindingIdIsStillAccountedFor` checks all 13 ids are still referenced in the two suites, so a repair cannot retire a finding by deleting the assertion that watched it.
- [x] 5.5 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`). Green with no warnings, per the pre-PR rule. — 100/100, no warnings.

## 6. Documentation and follow-through

- [x] 6.1 Update `findings.md` in `verify-recipe-editor-parity` with the final disposition of each finding — repaired, or retained with its reason.
- [x] 6.2 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md`: frames are the source of truth for recipe parameters, a stored block is a cache, and the four removed parameters are gone.
- [x] 6.3 ~~Update the wiki manual~~ — **not needed** (maintainer's call). The manual does not document per-profile numbers, so nothing in it becomes wrong: profiles now display their own values where they previously showed struct defaults, which is the manual's implied behaviour already.
- [x] 6.4 Re-decide `preserve-recipe-visualizer-roundtrip` now that `prep` exists: state plainly what, if anything, remains of it, and archive or rewrite it accordingly. — **evidence recorded; decision deliberately deferred until this change lands** (maintainer's call — do not archive it yet). On that evidence it is superseded: Its Why was real (REC-1, and broader than Visualizer), but the fix was frame→recipe reconstruction, which that change lists as an explicitly rejected Non-Goal. No `recipe` block on the wire, no schema, no upstream PRs. Three items survive and each is smaller on its own: the `target_volume_count_start` loss, the Visualizer TCL/JSON download choice, and `dose` (a parameter no plugin has and no frame carries). Banner written at the head of its Decisions section.
- [x] 6.5 Raise the design's second open question with the maintainer — whether to re-sync the five A-Flow built-ins from the plugin so their stale fabricated blocks disappear from the shipped files. That is a `resources/profiles/` edit and outside this change.
