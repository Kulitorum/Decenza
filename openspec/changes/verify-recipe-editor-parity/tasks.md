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

- [ ] 5.1 Assert frame roles resolve by `set_profile_index`'s rule for both the 9-frame and legacy 6-frame layouts.
- [ ] 5.2 Assert a 6-frame profile extracts the parameters the plugin extracts from it.
- [ ] 5.3 Assert editing and saving a 6-frame profile upgrades it the way `update_A-Flow` does, inserting `Pre Fill` and `2nd Fill`/`Pause` with the plugin's values.

## 6. Inheritance

- [ ] 6.1 Assert the parameters A-Flow inherits unchanged from D-Flow behave identically in both editors, so a shared-half regression fails in both rather than being masked in one.
- [ ] 6.2 Assert the documented divergences hold and are not swapped: A-Flow's soak temperature from fill temperature vs D-Flow's from pour temperature; A-Flow's fill flow of 8 ml/s.

## 7. Decenza-only parameters

- [ ] 7.1 For each of `fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled`, determine which plugin field it writes and whether the plugin ever writes that field. Record a verdict: deliberate extension or defect.
- [ ] 7.2 For each declared extension, add a test pinning what it does to a profile the plugin would round-trip untouched, so the cost is visible rather than incidental.
- [ ] 7.3 Assert no editor parameter exists with neither a plugin counterpart nor a recorded verdict.

## 8. Editor coverage

- [ ] 8.1 Assert each editor surfaces every parameter its plugin exposes (D-Flow: fill temperature, soak seconds/pressure/volume/weight, pour flow/pressure/temperature. A-Flow: those plus fill flow, ramp seconds, and the three toggles).
- [ ] 8.2 Assert changing one parameter moves the frame fields the plugin moves and no others.

## 9. Findings and gate

- [ ] 9.1 Write `findings.md`: every divergence with its plugin citation, its effect on a real profile, and a severity. State plainly which expectations are transcription-only (no shipped profile exercises them).
- [ ] 9.2 Commit confirmed-defect assertions as expected failures carrying finding ids — do not weaken an assertion to make the suite green.
- [ ] 9.3 Feed the reconstruction verdict back into `preserve-recipe-visualizer-roundtrip`: if frame-derived extraction is faithful, its D1/D3 and the whole `recipe`-block/three-app rollout need re-deciding.
- [ ] 9.4 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) before PR.

## 10. Documentation

- [ ] 10.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` with the reference relationship, the two plugin repos and their pinned commits, and the rule that the plugins are the oracle.
- [ ] 10.2 Record the parity suite in `docs/CLAUDE_MD/TESTING.md` alongside the other corpus-driven gates, including the "fixtures come from the plugin, not `de1plus/profiles/`" rule and why.
