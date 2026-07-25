## Why

Decenza re-implements two de1app profile-editor plugins — **D-Flow** (Damian-AU/D_Flow_Espresso_Profile) and **A-Flow** (Jan3kJ/A_Flow, which is derived from D-Flow) — as `RecipeParams` + `RecipeGenerator`. Nothing verifies that either re-implementation agrees with its original. Two discrepancies are already visible without looking hard:

- **The shipped A-Flow built-ins disagree with their own frames.** All five `A-Flow / default-*` JSONs carry a byte-identical `recipe` block claiming 88 °C fill / 25 s fill / 20 s infuse / 4 g / 9 bar. Their actual frames span 93–95 °C, 15 s fill, 6–60 s infuse, 2.0–6.0 g, 9.0–10.0 bar. The block matches none of them, and A-Flow's readme documents `default-very-dark` as `Ramp down` **enabled** while the block says `false` for all five. Opening one in Decenza's editor shows the wrong numbers; saving regenerates the frames from them.
- **The reconstruction premise was wrong.** The in-flight `preserve-recipe-visualizer-roundtrip` change rests on the decision that these parameters are "not losslessly derivable from the frames" and "cannot recover A-Flow toggles". Both plugins do exactly that: `proc prep` in each rebuilds the editor's entire state from the frames on every profile load. The frames *are* the storage — which is why no `.tcl` carries a recipe key.

Same gap behind both: nobody has checked Decenza against the references.

The two editors are verified in one effort because **A-Flow depends on D-Flow**. It is documented as "a Profile Editor based on D-Flow", and its readme states "Infuse parameters are not changed compared to D-Flow. Only the fill step is different with 8 ml/s flow" — so A-Flow's soak semantics are D-Flow's by definition, and a D-Flow finding is an A-Flow finding. (de1app ships both in its default `enabled_plugins`, so users have them together, but the dependency is one of lineage rather than code: neither plugin sources the other.) Verifying A-Flow alone would leave its inherited half unchecked, and verifying them separately would miss where they legitimately **diverge** — the sharpest example being that A-Flow sets the soak frame's temperature from the **fill** temperature while D-Flow sets it from the **pour** temperature. A swap there is invisible unless both are checked against their own reference.

## What Changes

- **Establish the plugins as the reference.** Both are submodules of the local de1app clone, read-only here. Decenza is verified against them, never the reverse.
  - D-Flow — `de1plus/plugins/D_Flow_Espresso_Profile` (`https://github.com/Damian-AU/D_Flow_Espresso_Profile`)
  - A-Flow — `de1plus/plugins/A_Flow` (`https://github.com/Jan3kJ/A_Flow`)
- **Verify D-Flow first, then A-Flow as its derivative.** The shared inheritance (infuse/soak parameters) is settled once against D-Flow; A-Flow's pass then covers only what it changes — the fill step, the pressure ramp, the extraction ramp, and its three structural toggles — plus an explicit check that the inherited half really is unchanged.
- **Frame-generation parity.** Assert Decenza's generator produces the frames each plugin's `update_*` proc produces from the same parameters — for D-Flow including its derived fill-pressure rule (`filling(pressure)` = soak pressure; `filling(exit_pressure_over)` = soak pressure when under 2.8, else `soak/2 + 0.6`, floored at 1.2), for A-Flow across the full toggle matrix and the ramp-time rounding edges.
- **Parameter-extraction parity.** Assert Decenza recovers from a profile's frames the same parameters each plugin's `proc prep` recovers — including A-Flow's three toggles, derived from frame structure (`ramp_down(seconds) > 0`; `pouring(flow) > pouring_start(flow)`; `pause(seconds) > 0` on a 9-frame layout) rather than stored anywhere.
- **Round-trip stability.** Assert `frames → parameters → frames` is a fixed point for every stock D-Flow and A-Flow profile: loading one and saving it unchanged must not alter a single frame field.
- **Both A-Flow frame layouts.** `set_profile_index` supports a 9-frame and a legacy 6-frame layout, and `update_A-Flow` upgrades the latter by inserting `Pre Fill` and `2nd Fill`/`Pause`. Verify Decenza handles both.
- **Classify Decenza-only parameters.** `RecipeParams` exposes `fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled`; neither plugin writes `filling(seconds)` or `filling(flow)`, and D-Flow *derives* `filling(pressure)` rather than exposing it. Establish for each whether it is a deliberate extension or an accident that overwrites a field the plugin preserves — the in-place-mutation vs regenerate-from-constants difference is the likely source of drift.
- **Editing capability.** Verify each editor surfaces the parameters its plugin exposes, and that changing one moves the same frame fields the plugin moves — no more, no less.
- **NOT doing:** changing any shipped profile JSON; changing either plugin; deciding the fate of `preserve-recipe-visualizer-roundtrip` (this change supplies the evidence that decision needs).

## Capabilities

### New Capabilities
- `recipe-editor-parity`: Verified agreement between Decenza's D-Flow and A-Flow implementations and their upstream de1app plugins — frame generation, parameter extraction from frames, round-trip stability, frame-layout handling, editor parameter coverage, and the inheritance relationship between the two editors.

### Modified Capabilities
<!-- None. This change verifies existing behaviour; requirement changes it uncovers are raised as findings rather than pre-declared. -->

## Impact

- **Reference (read-only):**
  - `D_Flow_Espresso_Profile/plugin.tcl` — `proc prep`, `proc update_D-Flow`, and the `write_*_profile` procs holding the stock D-Flow profiles (they are embedded in the plugin, not shipped as `.tcl` files).
  - `A_Flow/code.tcl` — `proc prep`, `proc update_A-Flow`, `proc set_profile_index`; `readme.md`; `profiles/A-Flow____default-*.tcl`.
- **Verified:** `src/profile/recipegenerator.cpp` (`generateDFlowFrames`, `generateAFlowFrames`, frame helpers), `src/profile/recipeparams.{h,cpp}`, `src/profile/profile.cpp` (`editorType`, `regenerateFromRecipe`), `src/controllers/profilemanager.cpp` (`getOrConvertRecipeParams`).
- **Tests:** a recipe-editor parity suite driven by both plugins' stock profiles, in the style of `tests/tst_tclimport.cpp`'s corpus-driven gates.
- **Data (read-only):** `resources/profiles/{a,d}_flow_*.json` are compared, not edited.
- **Feeds:** `openspec/changes/preserve-recipe-visualizer-roundtrip` — its D1/D3 decisions rest on a claim this change tests directly.
