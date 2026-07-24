## 1. Recipe interchange schema

- [ ] 1.1 Add a `version` marker to the `recipe` object in `RecipeParams::toJson()` and read/validate it in `RecipeParams::fromJson()` (unrecognized version → signal "treat as absent").
- [ ] 1.2 Write the `recipe` schema doc (fields, units, defaults, A-Flow toggles, version) — the interchange contract; include the de1app D-Flow/A-Flow ↔ recipe mapping table from design.md.

## 2. Decenza upload (producer)

- [ ] 2.1 In `VisualizerUploader::buildVisualizerProfileJson()` (visualizeruploader.cpp), emit `recipe = profile->recipeParams().toJson()` for recipe-based editors (dflow/aflow, and simple pressure/flow where recipe params are set). Do NOT add any editor-type field; type stays name-derived.
- [ ] 2.2 Confirm no `recipe` is emitted for hand-built advanced profiles.

## 3. Decenza import (consumer + fallback)

- [ ] 3.1 Verify `Profile::fromJson` reconstructs `m_recipeParams` from a present `recipe` block (already wired at profile.cpp:556); add version handling so an unrecognized `recipe.version` is treated as absent.
- [ ] 3.2 Implement the "no recipe ⇒ advanced" guard: a D-Flow/A-Flow-titled profile with no (valid) `recipe` block imports as an advanced profile — never a default-filled recipe editor. Ensure `editorType()` only resolves to dflow/aflow when a recipe actually backs it.
- [ ] 3.3 Remove/avoid the frames→recipe path (`RecipeAnalyzer`) from the Visualizer import flow so no reconstruction-from-frames occurs on import.

## 4. Preinfuse frame count fix

- [ ] 4.1 In `Profile::fromJson`, map `target_volume_count_start` → `m_preinfuseFrameCount` (fall back to `number_of_preinfuse_frames`/`preinfuse_frame_count`/`countPreinfuseFrames` as today) so the count round-trips instead of resetting to 0.

## 5. TCL file-download support (Decenza side)

- [ ] 5.1 Teach `Profile::loadFromTclFile` to parse an embedded `recipe` key from a `.tcl` profile (matching the Visualizer TCL encoding chosen in the PR), so the website TCL download round-trips into a faithful editor via `ProfileImporter`.
- [ ] 5.2 Confirm a `.tcl` download lacking `recipe` still imports as a working advanced profile (backward compatible).

## 6. Tests

- [ ] 6.1 Round-trip unit test: D-Flow `RecipeParams` → upload JSON (`buildVisualizerProfileJson`) → `Profile::fromJson` → identical `RecipeParams` (all fill/infuse/pour fields, targets).
- [ ] 6.2 Round-trip unit test: A-Flow with non-default `rampDownEnabled`/`flowExtractionUp`/`secondFillEnabled` → identical toggles and generated frames.
- [ ] 6.3 Test: D-Flow/A-Flow-titled profile with no `recipe` block imports as advanced (no default-filled editor).
- [ ] 6.4 Test: unrecognized `recipe.version` imports as advanced.
- [ ] 6.5 Test: preinfuse frame count of 2 survives upload→import (not 0).
- [ ] 6.6 Test: TCL profile with embedded `recipe` reconstructs; TCL without it imports as working advanced.
- [ ] 6.7 Run the full suite via `mcp__qtcreator__run_tests` (scope all) before PR.

## 7. Documentation

- [ ] 7.1 Update `docs/CLAUDE_MD/VISUALIZER.md` (round-trip behavior, recipe block, no-recipe⇒advanced, TCL embedding).
- [ ] 7.2 Update the wiki manual Visualizer-import entry for the faithful D-Flow/A-Flow round-trip.

## 8. External / ecosystem PRs (separate repos — coordinate, not landed in this repo)

- [ ] 8.1 Visualizer (miharekar/visualizer): add `recipe` to `JSON_PROFILE_KEYS` in `app/models/shot_information/profile.rb`; embed `recipe` in `tcl_profile`. Confirm `JSON_PROFILE_KEYS`/`PROFILE_FIELDS` byte-exact from `base.rb`/`profile.rb` before drafting. (JSON download-format option is a SEPARATE, deferred PR — do not include.)
- [ ] 8.2 de1app (producer): build a `recipe` huddle from D-Flow/A-Flow plugin state and attach it at `profile.tcl:459` / `shot.tcl:233`; source A-Flow toggles from `::ramp_down_enabled`/`::flow_extraction_up`/`::2nd_fill_step`, sliders from `::Dflow_*`/`::Aflow_*`. Coordinate schema with plugin authors.
- [ ] 8.3 reaprime (passthrough): stop dropping unknown keys in `Profile.fromJson`/`toJson` (`lib/src/models/data/profile.dart`) and the web import (`profile_handler.dart`) so a `recipe` survives routing through reaprime.

## 9. Verification

- [ ] 9.1 End-to-end: upload a user D-Flow and a user A-Flow from Decenza, download via the in-app share-code importer, confirm reconstructed recipe equals the original (via `profiles_get_detail` / D-Flow editor).
- [ ] 9.2 Clean up the throwaway `d_flow_q_vizimporttest` test profile created during investigation.
