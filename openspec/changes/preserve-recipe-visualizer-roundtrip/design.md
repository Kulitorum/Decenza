## Context

D-Flow (Damian Brakel) and A-Flow (Janek) are high-level espresso *recipe editors*: the user sets a handful of parameters (fill / infuse / pour pressures, flows, temperatures, times, plus A-Flow structural toggles) and the editor generates the low-level DE1 `advanced_shot` frames. They originated as **de1app plugins**; Decenza re-implements them as `RecipeParams` + `RecipeGenerator`.

visualizer.coffee stores the entire uploaded `profile` object verbatim in `shot_informations.profile_fields["json"]`, but its readback (`json_profile` / `tcl_profile`) rebuilds the response from a fixed whitelist (`JSON_PROFILE_KEYS`, ~16 keys) and drops everything else. Neither Decenza nor de1app uploads a high-level recipe today — only the flattened frames survive the round-trip.

Consequently, importing a D-Flow/A-Flow profile back from Visualizer produces a profile whose frames are correct but whose recipe editor is populated with **defaults** (the app tags it D-Flow/A-Flow from the title prefix, but has no parameters to load). The frames run correctly, yet the editor misrepresents the profile and saving from it regenerates default frames — silent corruption. This must be fixed for **arbitrary user-created** D-Flow/A-Flow profiles, and the downloaded profile file must be a working, faithful, editable profile in **any** DE1 app.

Investigations informing this design:
- **Visualizer** stores the uploaded `profile` verbatim; only the readback whitelist drops unknown keys (single-key fix in `app/models/shot_information/profile.rb`).
- **de1app** D-Flow is a 3-frame editor; A-Flow a 9-frame editor with three structural toggles (`ramp_down_enabled`, `flow_extraction_up`, `2nd_fill_step`). Its high-level state is *not* stored as a structured object — only the frames plus a raw `::settings` dump reach Visualizer. Upload huddle is built in `profile.tcl:443-460` (`legacy_profile_to_v2`) / `shot.tcl:218-233`. A-Flow is identified by `::settings(profile_editor) == "A_Flow"`; D-Flow only by the `"D-Flow /"` title prefix.
- **reaprime** (Flutter) has **no** recipe editor — only full frame-based v2 profiles. Its `Profile.fromJson`/`toJson` *drops* unknown keys, so a profile routed through it would strip a `recipe`. It is a passthrough concern, not a producer. **Correction (post-investigation):** reaprime does more than passthrough-strip — its `Profile.fromJson` *hard-rejects* any profile missing `tank_temperature` or `target_volume_count_start` (both of which Decenza's on-disk serializer omitted). So until `align-profile-json-with-reaprime` (Change 1) lands, reaprime cannot read a Decenza profile at all; the "passthrough" framing only becomes true once Change 1 ships. **This change depends on Change 1.**

## Goals / Non-Goals

**Goals:**
- Faithful round-trip of arbitrary D-Flow/A-Flow (and simple pressure/flow) recipe parameters through Visualizer upload → download.
- A single, additive, versioned `recipe` interchange object that any DE1 app can adopt.
- The downloaded profile *file* (website "Download this profile") is a valid, working, editable profile importable into any app via the standard File/Tablet path.
- Backward compatibility: apps that don't understand `recipe` still import a working profile from the authoritative frames.
- Fix the `target_volume_count_start` → preinfuse-frame-count loss.

**Non-Goals:**
- Frame→recipe reconstruction (inferring recipe parameters from the frames). It cannot recover A-Flow toggles or distinguish editor variants; explicitly rejected.
- Adding an editor-type field to the profile JSON — type stays carried by the name convention, preserving interchange-format compatibility.
- Building a recipe editor in reaprime, or forcing de1app to *read* the recipe (its producer PR is in scope to propose; its reader side is optional).
- Reconstructing recipe from de1app's raw `app.data.settings` dump.

## Decisions

### D1. Carry the recipe verbatim; do not reconstruct from frames
The recipe parameters are not derivable from the name (name = editor type only) and not losslessly derivable from the frames (the rejected `RecipeAnalyzer` path never sets `editorType` and cannot recover A-Flow toggles). The only faithful mechanism is to **carry the actual parameters** as a `recipe` object in the profile JSON. Import already consumes it: `Profile::fromJson` (profile.cpp:556) reconstructs `RecipeParams` from a `recipe` block. So the happy path is: add the block on upload, let Visualizer return it, and existing import logic rebuilds the editor exactly.

### D2. `recipe` is additive and backward-compatible; type stays in the name
`recipe` is an extra key in an otherwise-standard profile JSON. Apps that don't understand it ignore it and still import a working profile from the frames (which stay authoritative). The editor **type** continues to come from the `"D-Flow /"` / `"A-Flow /"` title prefix — no `editorType` field is added (that would break the de1app/Visualizer interchange format). `RecipeParams::toJson` is therefore left as-is (it already omits `editorType`), and `Profile::fromJson` keeps deriving the type from the title.

### D3. No recipe ⇒ import as advanced (never a default-filled editor)
When a D-Flow/A-Flow-titled profile arrives with no `recipe` block (foreign upload, or pre-change Decenza upload), import it as an **advanced** profile using its frames. The current bug is that `editorType()` claims dflow/aflow from the title alone even with no backing parameters; the fix is that a recipe editor is only presented when a `recipe` block actually backs it. This removes the silent-corruption path entirely, with no guessing.

### D4. Schema = Decenza `RecipeParams`, versioned, documented as the interchange contract
`RecipeParams::toJson()` is the concrete schema (fill/infuse/pour, A-Flow toggles, simple pressure/flow params, per-step temps). Add a `version` marker inside the `recipe` object; an unrecognized version imports as advanced (D3) rather than risking a misread. Publish the schema as a short spec doc so de1app/reaprime can target the same shape. It is a **superset** of de1app's editors — e.g. de1app D-Flow fixes fill flow (8) / fill timeout (25) and *derives* fill exit-pressure from infuse pressure, whereas Decenza exposes `fillPressure`/`fillFlow`/`fillTimeout`. The schema carries the explicit values; a consumer maps the fields it exposes and applies its own editor's fixed/derived rules for the rest (documented in the mapping table below).

### D5. Enable the round-trip in Visualizer: readback whitelist + a download-format option
Because the uploaded `profile` (including `recipe`) is already stored verbatim, the in-app API path (`?format=json` → `json_profile`) needs only `recipe` added to `JSON_PROFILE_KEYS` in `app/models/shot_information/profile.rb`. No migration. Offer the maintainer the minimal one-key add, or the more future-proof variant (return the stored `profile_fields["json"]` merged with the notes-stamp instead of rebuilding from a whitelist, so any app's extra keys survive).

The website **"Download this profile" file is TCL by default**, so covering the downloaded *file* means carrying `recipe` in the TCL serialization:
- **`tcl_profile` embeds `recipe`** — as a nested Tcl dict, or a JSON-string value under a `recipe` key. Decenza's `Profile::loadFromTclFile` (used by `ProfileImporter` for `.tcl`; it already accepts both `.tcl` and `.json`) is taught to parse that key so the TCL download round-trips into a faithful editor. This keeps the downloaded file a working profile for any app: de1app reads its own TCL, Decenza reconstructs the recipe from the embedded key, and apps that don't understand it ignore the key and still import the frames.
- **Deferred to a separate PR (out of scope here):** adding a JSON download-format *option* to the website (a JSON "Download this profile" variant). Decenza and reaprime are JSON-native and would benefit, but it is an independent enhancement and is intentionally not bundled into this change. This change relies on the JSON **API** readback (`?format=json`) for the in-app importers and TCL embedding for the file download.

### D6. Cross-app roles
- **Decenza** — producer + consumer: emit `recipe` on upload; reconstruct on import (both share-code and file paths, via `Profile::fromJson`).
- **de1app** — proposed producer: build a `recipe` huddle from the plugin state and attach it at `profile.tcl:459` / `shot.tcl:233`. A-Flow toggles come from `::ramp_down_enabled`/`::flow_extraction_up`/`::2nd_fill_step`; D-Flow/A-Flow sliders from the `::Dflow_*`/`::Aflow_*` globals. Reading it back is optional/future.
- **reaprime** — passthrough: stop dropping unknown keys in `Profile.fromJson`/`toJson` (`lib/src/models/data/profile.dart`) and the web import (`profile_handler.dart`) so a profile routed through reaprime keeps its `recipe`. No editor work needed.

### de1app ↔ Decenza field mapping (for the schema doc)
| Recipe field | de1app D-Flow | de1app A-Flow |
|---|---|---|
| fillTemperature | `Dflow_filling_temperature` | `Aflow_filling_temperature` |
| fillPressure | derived from infuse pressure (fixed rule) | from `filling`/`ramp` frames |
| fillFlow | fixed 8 | `Aflow_filling_flow` |
| fillTimeout | fixed 25 | frame `filling(seconds)` |
| infuseEnabled | always true | always true (frame present) |
| infusePressure / infuseTime / infuseWeight / infuseVolume | `Dflow_soaking_*` | `Aflow_soaking_*` |
| pourTemperature | `Dflow_pouring_temperature` | `Aflow_pouring_temperature` |
| pourFlow | `Dflow_pouring_flow` | `Aflow_pouring_flow` |
| pourPressure | `Dflow_pouring_pressure` | `Aflow_pouring_pressure` |
| rampTime | fixed | `Aflow_ramp_updown_seconds` |
| rampDownEnabled / flowExtractionUp / secondFillEnabled | n/a | `::ramp_down_enabled` / `::flow_extraction_up` / `::2nd_fill_step` |
| targetWeight / targetVolume / dose | `final_desired_shot_weight_advanced` / `..volume..` / `grinder_dose_weight` | same |
| preinfuse count | `final_desired_shot_volume_advanced_count_start` | same |

## Risks / Trade-offs

- **Visualizer PR must merge and deploy before the round-trip closes.** → Ship Decenza's upload first (recipes start being stored immediately, readable retroactively once the server change lands). Until then, Decenza→Decenza either waits or uses a notes-smuggle stopgap (ugly; not recommended as the destination).
- **Schema divergence across apps** (de1app derives some fields Decenza stores explicitly). → The `version`-marked schema doc is the contract; consumers map exposed fields and apply their own fixed/derived rules for the rest. Round-trip within one app is always exact; cross-app is faithful for the fields both model.
- **reaprime silently strips unknown keys today**, so routing a profile through it breaks the chain. → Passthrough PR to preserve unknown keys; until merged, document that reaprime is not recipe-preserving. (Separately, reaprime *rejects* Decenza profiles outright until Change 1 adds `tank_temperature`/`target_volume_count_start` — the `recipe` passthrough only matters once files are readable at all.)
- **de1app-uploaded D-Flow/A-Flow profiles have no `recipe`** and (per D3) import as advanced in Decenza rather than reconstructing. → Accepted: consistent "no guessing" behavior; improves automatically once de1app adopts the producer PR.
- **The website file download is TCL**, so the JSON whitelist fix alone does not cover it. → Embed `recipe` in `tcl_profile` and parse it in `Profile::loadFromTclFile`. (A JSON download option is deferred to a separate PR.)

## Migration Plan

1. Decenza: emit `recipe` (with `version`) on upload; add the no-recipe⇒advanced import guard; fix the preinfuse-count mapping. Ship — forward/backward compatible on its own.
2. Publish the `recipe` schema doc; align with de1app plugin authors / Visualizer maintainer.
3. Visualizer PR (readback whitelist). On deploy, Decenza→Decenza round-trip becomes exact (including retroactively-stored recipes).
4. de1app producer PR; reaprime passthrough PR.
Rollback: the upload key and import guard are independent and individually revertible; no persisted-data migration, so rollback is code-only.

## Open Questions

- TCL `recipe` encoding: nested Tcl dict vs a single JSON-string value under a `recipe` key — pick the one that's least invasive to Visualizer's `tcl_profile` and cleanest for `Profile::loadFromTclFile` to parse.
- Final `recipe` schema `version` string and where it's documented (in-repo doc vs a shared community spec).
- Scope/timing of the deferred JSON download-format option (separate PR) — track but do not implement here.
- Should Decenza also preserve unknown top-level profile keys through `Profile::fromJson`/`toJson` for its own passthrough friendliness (mirrors the reaprime concern)?
- Confirm byte-exact `JSON_PROFILE_KEYS` / `PROFILE_FIELDS` in Visualizer before drafting that PR (the source was read via a summarizing fetch).
