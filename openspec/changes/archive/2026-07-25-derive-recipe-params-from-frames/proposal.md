## Why

`verify-recipe-editor-parity` measured Decenza's D-Flow and A-Flow editors against the upstream plugins. The headline number is the edit matrix: **every plugin-exposed parameter × every stock profile, 99 cases, 13 match and 86 diverge.** The comparison is against frames produced by de1app's own `prep` + `update_*` procs, so it is not a transcription judgement.

**69 of the 86 divergences are a single field: the fill frame's temperature.** It is not a rounding difference or an edge case — it is a struct default overwriting the profile:

```
D-Flow / La Pavoni, editing pour flow:
  frame 0 (Filling) temperature: decenza 88, de1app 84
```

La Pavoni's fill is 84 °C. Editing an unrelated parameter rewrites it to 88 °C, the value in `RecipeParams`'s member initialiser. Every A-Flow built-in shows the same shape.

Two mechanisms produce it:

1. **A recipe block is fabricated for any profile the title makes a recipe profile.** `Profile::toJsonObject()` writes `obj["recipe"] = m_recipeParams.toJson()` whenever `editorType()` is `dflow`/`aflow` — and `editorType()` derives from the title. A profile imported from a `.tcl`, which carries no recipe by design, gets one built from a never-populated `RecipeParams`. `getOrConvertRecipeParams()` then takes its first branch (`targetWeight > 0`, and the default is 36.0) and returns the fabricated block instead of consulting the frames. This is also the proven origin of the five byte-identical `recipe` blocks in the A-Flow built-ins, which match none of their own frames.

2. **`RecipeAnalyzer` is a D-Flow analyzer applied to A-Flow.** It pattern-matches a three-frame shape against a nine-frame profile: pour flow from the wrong frame (2× the real value), ramp time not summed across both ramp frames, fill timeout read from `Pre Fill`, and two of the three structural toggles either mis-derived or never derived at all.

The upstream fix for (2) is known and small. Both plugins reconstruct their entire editor state from the frames on every profile load, in a `proc prep` of a few dozen lines — the frames *are* the storage, which is why no `.tcl` carries a recipe key. Decenza needs the same reconstruction rather than a better guess.

What is **not** wrong is worth stating, because it bounds the work: the generation rules are faithful (all 8 A-Flow toggle combinations match the plugin field for field), the editor surfaces exactly the plugins' parameter set and nothing extra reaches the UI, the legacy 6-frame upgrade path is correct, and the BLE encoders agree with de1app's real packer on 120 boundary-stressing profiles with **zero** quantisation differences. The defect is concentrated in the layer that decides *which parameters* to hand the generator.

## What Changes

- **Stop fabricating recipe blocks (REC-1).** A recipe block is written only when the parameters were actually established — from frames or from a user edit — never from a default-constructed struct. `getOrConvertRecipeParams()` derives from frames when no genuine block exists, rather than treating a title match as evidence of stored parameters.
- **Implement `prep` (AF-1 … AF-5).** Replace A-Flow's use of the D-Flow pattern analyzer with a direct transcription of `A_Flow/code.tcl`'s `prep`: pour flow from `Flow Start`, ramp time summed across both ramp frames, and all three toggles derived from frame structure — `ramp_down(seconds) > 0`, `pouring(flow) > pouring_start(flow)`, and the pause frame's duration on a 9-frame layout. Frame roles resolve positionally through `set_profile_index`, never by name or pattern.
- **Remove the four vestigial parameters (AF-6, §7).** `fillTimeout`, `fillPressure`, `fillFlow` and `infuseEnabled` have no plugin counterpart, appear nowhere in QML, and `fillTimeout` actively overwrites `filling(seconds)` — a field neither plugin's `update_*` ever writes. The parity change recorded all four as defects, not extensions.
- **Stop writing frame fields the plugins leave alone (DF-1, DF-2, DF-5).** `filling(volume)`, `filling(weight)` and `pouring(volume)` are generated from constants rather than preserved. `pouring(volume)` forced to 0 *removes* a stop cap, since the firmware treats `MaxVol 0` as "ignore".
- **Fix the one wire-level difference (WIRE-1).** The packed tail's `MaxTotalVolume` marker byte is `0x04` where de1app sends `0x00`.
- **Adopt the edit matrix as the acceptance gate.** It stands at 13/99. The change is complete when the remaining divergences are zero or each is a named, justified difference — not when the suite is merely green, which it already is.
- **NOT doing:** changing any shipped profile JSON (they mirror upstream repos Decenza does not own); changing either plugin; retro-rewriting recipe blocks already stored in users' saved profiles; adding any user-facing setting.

## Capabilities

### New Capabilities
<!-- None. -->

### Modified Capabilities
- `recipe-editor-parity` (introduced by `verify-recipe-editor-parity`, which must archive first): the divergences that capability records as findings become requirements Decenza meets. Extraction is specified as frame-derived rather than block-derived, a recipe block is specified as a cache that never overrides frames, and the four Decenza-only parameters are removed rather than declared.

## Impact

- **Changed:** `src/profile/recipeanalyzer.{h,cpp}` (A-Flow extraction rewritten as `prep`), `src/profile/profile.cpp` (`toJsonObject` recipe-block emission, `regenerateFromRecipe`), `src/controllers/profilemanager.cpp` (`getOrConvertRecipeParams`), `src/profile/recipeparams.{h,cpp}` (four fields removed), `src/profile/recipegenerator.cpp` (fields the plugins do not write), the BLE frame packer (WIRE-1).
- **Gate:** `tests/tst_recipeeditorapppath.cpp` (edit matrix), `tests/tst_recipeeditorparity.cpp` — the expected-failures carrying finding ids become passes as each is repaired.
- **User-visible:** opening a stock A-Flow or D-Flow profile shows the profile's own numbers instead of struct defaults; saving one unedited stops altering it. Editing any parameter stops resetting the fill temperature.
- **Unblocks:** `preserve-recipe-visualizer-roundtrip`, whose premise this refutes — a working `prep` closes the Visualizer round-trip with no schema change, no `recipe` block on the wire, and no upstream PRs.
- **Read-only reference:** `de1plus/plugins/{A_Flow,D_Flow_Espresso_Profile}` and their stock profiles.
