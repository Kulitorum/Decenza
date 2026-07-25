> **Order matters.** D-Flow is verified first because A-Flow is derived from it ("Infuse parameters are not changed compared to D-Flow"), so a shared-half defect must be diagnosed once, in the editor that owns it. §6 then checks that the inheritance actually holds.
>
> **Oracle discipline (design D1/D2).** Every expected value traces to a plugin proc or a plugin-shipped profile — never to Decenza's code or its built-in JSONs. Fixtures come from the plugin `profiles/` directories; `de1plus/profiles/` holds a stale 6-frame A-Flow snapshot (de1app #350) and is not the reference.

## 1. Reference capture

- [x] 1.1 Record the pinned submodule commits for both plugins (`A_Flow` at `e1a4d87` at time of writing; capture D-Flow's) in the parity suite, so a plugin bump is visible rather than silent. — A_Flow `e1a4d871`, D_Flow `7f3c9726`, de1app `fe5cf40c`.
- [x] 1.2 Transcribe D-Flow's `proc prep` and `proc update_D-Flow` (`D_Flow_Espresso_Profile/plugin.tcl`) into a commented reference table: which parameter reads which frame field, which frame fields each write touches, and the derived fill-pressure rule. Cite line numbers. — `reference.md`.
- [x] 1.3 Transcribe A-Flow's `proc prep`, `proc update_A-Flow`, and `proc set_profile_index` (`A_Flow/code.tcl`) the same way, including both frame layouts, the three toggle derivations, and the ramp-time split with its rounding. — `reference.md`.
- [x] 1.4 Extract D-Flow's stock profiles from the `write_*_profile` procs into test data with provenance recorded — they are embedded in Tcl, not shipped as files. Confirm whether those are the complete set Decenza ships as `d_flow_*.json`. — `tools/extract_dflow_profiles.py` → `tests/data/dflow_plugin_profiles/` (default, Q, La Pavoni; all 3 frames), provenance + the reconstructed-`default` caveat in that dir's README. Matches the three `d_flow_*.json` built-ins.
- [x] 1.5 Wire A-Flow's five stock `.tcl` files from the plugin's `profiles/` directory in as fixtures. Assert at load that each has 9 frames, so a stale 6-frame copy cannot silently become the oracle. — `tests/data/de1app_profiles/A-Flow____*.tcl` verified byte-identical to the plugin copies and all 9 frames; `de1plus/profiles/` confirmed stale (4 files at 6 frames, `default-light` missing entirely).

## 2. D-Flow parity

- [x] 2.1 Extraction: for each stock D-Flow profile, assert Decenza recovers the parameters `prep` recovers — including pour pressure from the pour frame's `max_flow_or_pressure` (not its `pressure`), and soak temperature semantics. — passes on all three stock profiles, including pour pressure from the limiter.
- [x] 2.2 Generation: assert Decenza's generated frames match `update_D-Flow`'s output field for field, including `espresso_temperature` being set from the **fill** temperature. — passes.
- [x] 2.3 Derived fill pressure: assert fill pressure equals soak pressure, and the pressure-over exit follows `soak < 2.8 ? soak : soak/2 + 0.6`, floored at 1.2. Cover all three branches including the floor. — passes on all six branches (threshold, formula, floor).
- [x] 2.4 Untouched fields: assert no frame field that `update_D-Flow` never writes (notably `filling(seconds)` and `filling(flow)`) is altered by a Decenza generate. — **DF-1/DF-2/DF-4/DF-5**: `filling(volume)`, `filling(weight)`, `soaking(exit_pressure_over)` and `pouring(volume)` are all rewritten. See findings.md.
- [x] 2.5 Round-trip: assert load → save-unedited is a fixed point on every stock D-Flow profile. — fixed point on no stock profile; **DF-3** is upstream (plugin data contradicts its own rule), the rest are ours.

## 3. A-Flow parity — extraction

- [x] 3.1 Assert Decenza recovers each `Aflow_*` parameter as `prep` does, including `ramp_updown_seconds` as the **sum** of the ramp-up and ramp-down frame durations, and pour flow from the `Flow Start` frame. — **AF-1** (pourFlow from Flow Extraction, not Flow Start: 2x) and **AF-3** (rampTime not summed) and **AF-5** (fillTimeout from Pre Fill).
- [x] 3.2 Assert the three toggles are derived from frame structure, not read from stored data: ramp-down from a non-zero decline duration, flow-up from extraction flow exceeding pour flow, second-fill from a 9-frame layout with a non-zero pause duration. — **AF-2**/**AF-4**: flowExtractionUp mis-derived, rampDownEnabled never derived at all.
- [x] 3.3 Assert extraction works with no recipe block present at all — the frames alone must be sufficient, which is the property the plugins rely on. — passes: the .tcl fixtures carry no recipe and extraction still yields real values.
- [x] 3.4 Assert `A-Flow / default-very-dark` extracts `rampDownEnabled == true`, per the plugin readme and its frames. (Decenza's shipped `recipe` block claims `false` for all five profiles — this task is expected to surface that as a finding.) — **AF-4 confirmed**: extracts false; readme and frames both say true.

## 4. A-Flow parity — generation

- [x] 4.1 Assert generated frames match `update_A-Flow` across all 8 toggle combinations, field for field. — **passes** all 8 combinations; the rules are faithful.
- [x] 4.2 Assert the ramp split matches: ramp-up and ramp-down durations, and the odd-value remainder going to the decline frame. — **passes**, including integer division and the odd remainder going to the decline.
- [x] 4.3 Assert the derived exit thresholds: ramp-up `exit_flow_over` (pour flow, doubled when ramp-down is on), decline `exit_flow_under` (pour flow + 0.1), and `Flow Start` activation when ramp-up is under 1 s with its `exit_flow_over` of pour flow − 0.1. — **passes**: ramp-up exit, decline exit, and Flow Start activation on the post-split duration.
- [x] 4.4 Assert extraction flow is doubled pour flow when flow-up is on and zero when off, and that the extraction limiter carries the pour pressure. — **passes**: doubled when flow-up, zero when off, limiter carries pour pressure.
- [x] 4.5 Untouched fields: assert Decenza does not write frame fields `update_A-Flow` leaves alone — the in-place-mutation vs build-from-constants difference (design Context) makes this the highest-yield check in the change. — **AF-6**: `filling(seconds)` written from the invented `fillTimeout` (15 -> 25). Only this one field; found by isolating generation with prep-correct params.
- [x] 4.6 Round-trip: assert load → save-unedited is a fixed point on all five stock A-Flow profiles. — not a fixed point on ANY of the five; AF-1's flow error compounds per save.

## 5. Frame layouts

- [x] 5.1 Assert frame roles resolve by `set_profile_index`'s rule for both the 9-frame and legacy 6-frame layouts. — passes; both layouts resolve by position.
- [x] 5.2 Assert a 6-frame profile extracts the parameters the plugin extracts from it. — one error only, AF-1 again. Notably AF-5 does NOT occur: no Pre Fill frame to land on.
- [x] 5.3 Assert editing and saving a 6-frame profile upgrades it the way `update_A-Flow` does, inserting `Pre Fill` and `2nd Fill`/`Pause` with the plugin's values. — passes; inserted Pre Fill / 2nd Fill / Pause carry the plugin's literals.

## 6. Inheritance

- [x] 6.1 Assert the parameters A-Flow inherits unchanged from D-Flow behave identically in both editors, so a shared-half regression fails in both rather than being masked in one. — passes.
- [x] 6.2 Assert the documented divergences hold and are not swapped: A-Flow's soak temperature from fill temperature vs D-Flow's from pour temperature; A-Flow's fill flow of 8 ml/s. — passes; the soak-temperature divergence holds and is not swapped.

## 7. Decenza-only parameters

- [x] 7.1 For each of `fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled`, determine which plugin field it writes and whether the plugin ever writes that field. Record a verdict: deliberate extension or defect. — all four are **defects, not extensions** (see 8.1: none is user-facing).
- [x] 7.2 For each declared extension, add a test pinning what it does to a profile the plugin would round-trip untouched, so the cost is visible rather than incidental. — pinned for each.
- [x] 7.3 Assert no editor parameter exists with neither a plugin counterpart nor a recorded verdict. — asserted.

## 8. Editor coverage

- [x] 8.1 Assert each editor surfaces every parameter its plugin exposes (D-Flow: fill temperature, soak seconds/pressure/volume/weight, pour flow/pressure/temperature. A-Flow: those plus fill flow, ramp seconds, and the three toggles). — editor binds exactly the plugins' parameter set; the four Decenza-only params appear nowhere in QML.
- [x] 8.2 Assert changing one parameter moves the frame fields the plugin moves and no others. — covered by the generation tests in section 4.

## 9. Findings and gate

- [x] 9.1 Write `findings.md`: every divergence with its plugin citation, its effect on a real profile, and a severity. State plainly which expectations are transcription-only (no shipped profile exercises them). — `findings.md`.
- [x] 9.2 Commit confirmed-defect assertions as expected failures carrying finding ids — do not weaken an assertion to make the suite green. — 11 findings live as XFAILs with ids.
- [x] 9.3 Feed the reconstruction verdict back into `preserve-recipe-visualizer-roundtrip`: if frame-derived extraction is faithful, its D1/D3 and the whole `recipe`-block/three-app rollout need re-deciding. — banner added at the head of that change's D1; premise refuted, change parked pending re-decision.
- [x] 9.4 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) before PR. — **99/99 green, no warnings**; nothing had pinned RecipeAnalyzer's behaviour.

## 10. Documentation

- [x] 10.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` with the reference relationship, the two plugin repos and their pinned commits, and the rule that the plugins are the oracle. — reference relationship, both repos + pinned commits, the three non-obvious plugin facts, and the #350 fixture rule.
- [x] 10.2 Record the parity suite in `docs/CLAUDE_MD/TESTING.md` alongside the other corpus-driven gates, including the "fixtures come from the plugin, not `de1plus/profiles/`" rule and why. — parity gate documented incl. oracle discipline, the fixture table, and the XFAIL-not-relax rule.

## 11. Differential wire and quantisation tests

- [x] 11.1 Stand up a standalone oracle that runs de1app's **real** `de1_packed_shot` (`de1plus/binary.tcl`) outside the app, with inert shims for the UI-only helpers it calls, so BLE frame bytes can be compared rather than transcribed. — `tools/de1app_pack_oracle.tcl`; four shims (`ifexists`, `translate`, `msg`, rounding) plus `package provide` stubs, each listed in the file so a shim that ever grows teeth is visible.
- [x] 11.2 Assert Decenza's encoders produce byte-identical frames to that oracle for all 8 stock profiles. — `tests/data/de1app_packed/`; header and every frame identical except one byte. **WIRE-1**: the tail's `MaxTotalVolume` marker byte is `0x04` where de1app sends `0x00`.
- [x] 11.3 Property-test the encoders across the quantisation boundaries the stock profiles never reach — U8P4 and U8P1 ties, the F8_1_7 switchover at 12.75, the U10P0 1023/1024 wrap. — `tools/gen_pack_property_corpus.py` (seeded, 120 profiles) + `tests/data/pack_property/`; **zero** quantisation divergences. WIRE-1 is the only difference and it is positional, not numeric.

## 12. Edit matrix — every parameter × every stock profile

- [x] 12.1 Stand up an edit oracle that runs each plugin's **own** `prep` + `update_*` on a real profile with one parameter changed. Both plugin files are GUI code and cannot be sourced, so extract those procs by brace-matched text and eval them verbatim. — `tools/de1app_edit_oracle.tcl`. Two Tcl brace traps documented in-file (a lone `{` in a comment, a `}` inside a character class) — either swallows the rest of the file.
- [x] 12.2 Generate a golden per (profile, parameter) pair for every parameter the plugins expose: 8 shared × 8 profiles plus 7 A-Flow-only × 5 profiles. — `tools/gen_edit_matrix.py` → 99 goldens in `tests/data/edit_matrix/`.
- [x] 12.3 Drive the same edits through `ProfileManager`'s `Q_INVOKABLE`s — the path the editor and MCP both use — and diff every frame field, collecting all divergences rather than the first. — `editMatrixMatchesDe1app` in `tests/tst_recipeeditorapppath.cpp`.
- [x] 12.4 Record the result and reconcile it against the per-layer findings. — **13 of 99 match, 86 diverge**; 69 of the 86 are the fill frame's temperature alone. Two corrections follow: **AF-7 is not A-Flow-specific — renamed REC-1** (D-Flow / La Pavoni writes fill 88 °C where the profile says 84 °C while editing pour flow), and the earlier "edited D-Flow is ~70% right" assessment is withdrawn — it rested on a test that asserts pressure fields and never reads temperature.
- [x] 12.5 Adopt the matrix as the acceptance gate for the repair change: 13/99 now, near 99/99 after REC-1 and `prep`, with any residue a named defect rather than an unknown. — recorded in `findings.md` § "Revised repair order".

## 13. Full-corpus regression guard

Added in response to the risk the repairs create: the parity fixtures are eight
recipe profiles, but the repairs touch load/save code every profile passes through.

- [x] 13.1 Extend the pack oracle to run de1app's **real** load path for simple profiles — `pressure_to_advanced_list` / `flow_to_advanced_list` from `profile.tcl`, seeded with `machine.tcl`'s default `::settings` and `vars.tcl`'s `profile_vars`, all extracted verbatim rather than transcribed. A simple profile's stored `advanced_shot` is a stale by-product; de1app rebuilds the frames at load and packs those.
- [x] 13.2 Pack all 89 stock de1app profiles with that oracle. — `tools/gen_de1app_pack_corpus.py` → `tests/data/de1app_packed/`.
- [x] 13.3 Assert Decenza's encoders produce identical bytes for every one, with WIRE-1 filtered **positionally** so any other differing byte fails hard. — `everyDe1appProfilePacksIdentically`; **89/89 identical**, WIRE-1 aside, across 63 advanced + 17 pressure + 9 flow profiles. Two independent implementations agree byte-for-byte, including the simple-profile frame regeneration nothing had compared before.
- [x] 13.4 Pin WIRE-1's universality (`wire1Profiles == compared`) so the filter cannot go blind and swallow a real divergence.
