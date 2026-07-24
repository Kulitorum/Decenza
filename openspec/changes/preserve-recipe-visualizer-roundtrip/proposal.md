> **Sequencing:** third in the profile-interop trilogy. Depends on `align-profile-json-with-reaprime` (Change 1 — format readability) and pairs with `sync-builtin-profiles` (Change 2 — content equivalence). This change carries the high-level `recipe` block through Visualizer; it assumes Change 1's aligned, reaprime-readable format is in place.

## Why

D-Flow and A-Flow profiles downloaded from visualizer.coffee do not come back as the profile that was uploaded. Visualizer stores only the flattened advanced frames, so on import the app re-labels the profile as D-Flow/A-Flow (from the title prefix) but has no recipe parameters to load — it fills the editor with **defaults**. The frames still run correctly, but the D-Flow/A-Flow editor shows wrong values (e.g. 88 °C / 3 bar / 20 s instead of 84 °C / 6 bar / 1 s), and opening the editor and saving regenerates frames from those defaults, silently destroying the profile. This must work for **arbitrary** user-created D-Flow/A-Flow profiles, not just the built-ins, and frame→recipe reconstruction is explicitly out of scope because it cannot be lossless (it cannot recover A-Flow structural toggles or the editor type).

## What Changes

- **Upload carries the recipe.** The Visualizer upload embeds a `recipe` object (the high-level editor parameters) inside the uploaded profile JSON. The editor *type* continues to be carried by the profile name convention ("D-Flow /…", "A-Flow /…"), not a JSON field — no new type key, no interchange-format break.
- **Import reconstructs from the recipe, or falls back to advanced.** When a downloaded profile carries a `recipe` block, the app rebuilds the D-Flow/A-Flow editor exactly from it. When it does **not** (foreign uploads, or shots uploaded before this ships), the app imports it as an **advanced** profile — never a default-filled D-Flow/A-Flow editor. No guessing.
- **The downloadable file is a complete, working profile for any app.** The profile file served by the Visualizer website's "Download this profile" link (not only Decenza's in-app share-code importer) carries the `recipe` and imports as a faithful, editable, immediately-usable profile through the standard File/Tablet import path — into **any** DE1 app. The `recipe` is an **additive, backward-compatible** key: an app that understands it reconstructs the exact D-Flow/A-Flow editor; an app that does not simply ignores it and still imports a fully working profile from the authoritative frames. This is why the frames stay authoritative and no editor-type field is added. Because Decenza's share-code importer and file importer share `Profile::fromJson`, one payload covers both Decenza paths; the recipe must be present in whichever serialization the download serves (JSON is primary; the TCL/`.tcl` download variant is a Visualizer-side consideration).
- **Preinfuse-frame-count mapping fix.** On import, map Visualizer's `target_volume_count_start` to the preinfuse frame count so it stops silently resetting to 0.
- **Shared `recipe` interchange schema.** Define and document a small, versioned `recipe` JSON schema (D-Flow, A-Flow, and the simple pressure/flow editors) so it is an ecosystem interchange format rather than a Decenza-private field.
- **NOT doing:** frame→recipe reconstruction (lossy); adding an `editorType`/type field to the profile JSON (breaks compatibility).
- **Coordinated, out-of-repo (tracked here for context, PRs land in their own repos):** a one-key readback-whitelist change in visualizer.coffee so the stored `recipe` is returned on download; and adoption of the same `recipe` block by de1app (D-Flow + A-Flow plugins via `visualizer_upload`) and reaprime, so profiles created in any app round-trip everywhere.

## Capabilities

### New Capabilities
- `visualizer-recipe-roundtrip`: Preservation and reconstruction of the high-level D-Flow/A-Flow (and simple pressure/flow) recipe parameters across a visualizer.coffee upload→download round-trip, via a shared `recipe` block, with an advanced-profile fallback when no recipe is present.

### Modified Capabilities
<!-- No existing spec's requirements change; the import fallback and upload payload are introduced as new requirements under the new capability above. -->

## Impact

- **Upload:** `src/network/visualizeruploader.cpp` (`buildVisualizerProfileJson`) — emit the `recipe` object.
- **Import:** `src/network/visualizerimporter.cpp` (`parseVisualizerProfile`) and `src/profile/profile.cpp` (`Profile::fromJson`) — consume `recipe` when present (already partially wired); when absent, import D-Flow/A-Flow-titled profiles as advanced; map `target_volume_count_start` → preinfuse frame count.
- **Schema:** `src/profile/recipeparams.{h,cpp}` (`RecipeParams::toJson`/`fromJson`) is the serialization contract; a companion doc describes the interchange schema. No new Decenza-only field is added to the standard profile JSON.
- **External repos (separate PRs):** miharekar/visualizer (readback whitelist), de1app D-Flow/A-Flow/`visualizer_upload` plugins, tadelv/reaprime — adopt the shared `recipe` block.
- **Docs:** update `docs/CLAUDE_MD/VISUALIZER.md` (import/round-trip behavior) and the wiki manual entry for Visualizer import.
- **Tests:** round-trip unit tests (D-Flow and A-Flow recipe → upload JSON → import → identical `RecipeParams`); no-recipe → advanced; preinfuse-count mapping.
