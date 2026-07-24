> **STATUS: STUB / follow-on.** Blocked on `align-profile-json-with-reaprime` (Change 1) landing first. This file records intent so it isn't lost; design.md, specs, and tasks are to be fleshed out once the aligned format ships and the case-by-case audit begins. Do not implement yet.

## Why

The Decent community needs a profile to make the **same coffee in every app**. Change 1 (`align-profile-json-with-reaprime`) makes Decenza's profiles *readable* by reaprime; this change makes the built-in profiles *equivalent* in content, so importing a shared profile yields the identical extraction regardless of which app authored it.

The ~63 built-in profiles common to Decenza and reaprime are **not** identical today: ~14 differ in step count (e.g. A-Flow 6 vs 9 frames), and ~50 have real value differences, on top of encoding/omitted-zero noise. Neither side is automatically authoritative: reaprime's set was pulled from Visualizer + de1app copy-exports and is documented (their issue #242 curation) to have contained stale notes, duplicate collisions, and **lever-profile corruption** (importing de1app's stale `advanced_shot` instead of the `settings_2a` intent). Decenza is likely the more faithful side for several (A-Flow 9-frame matches de1app's editor; levers), while reaprime may be cleaner for others. So sync is **bidirectional and case-by-case**, not a one-way overwrite.

## What Changes

- **Extend `tools/profile_sync.cpp` to a 3-way compare** — de1app-Tcl ↔ Decenza-JSON ↔ reaprime `assets/defaultProfiles` (JSON) — to surface every divergence in one report and drive the audit.
- **Harden the `settings_2a` path** to prefer settings-derived frames over a stale `advanced_shot` (the lever-corruption lesson from reaprime #242), in the parser/sync path (`src/profile/profile.cpp`).
- **Per-profile audit doc** classifying each common profile keep / metadata-fix / content-fix / dedup / needs-decision. Divergent profiles are reviewed **case by case** with the user — no blanket "de1app wins" or "reaprime wins" rule. Suspect heuristic: any `settings_2a` profile carrying explicit frames is flagged until reviewed.
- **Content + dedup fixes to Decenza built-ins** where warranted (`resources/profiles/*.json`), plus a check for the same collisions reaprime found (Sencha/Sensha, Chinese-green/white-tea, Bug-Bite/oolong-dark, milky=straight byte-copies).
- **A one-time user migration** so users who imported a now-corrected built-in receive the fixed version (Decenza's equivalent of reaprime's M1 metadata-refresh / M2 retire-list).
- **Upstream PRs to tadelv/reaprime** for profiles where Decenza is the more faithful side (A-Flow 9-frame; any lever/param corrections), updating their `assets/defaultProfiles/` + `manifest.json`.
- **Sourcing dependency:** obtain Visualizer canonical JSON for genuinely-disputed profiles where neither app's frames are trustworthy (reaprime was blocked on this too — lever re-port, milky differentiation).
- **NOT doing:** the serialization-format work (Change 1) and the `recipe` round-trip (Change 3, `preserve-recipe-visualizer-roundtrip`).

## Capabilities

### New Capabilities
- `builtin-profile-sync`: Cross-app content equivalence of the bundled profile set — a tooling-driven, case-by-case reconciliation of Decenza's built-ins against reaprime (and de1app/Visualizer as references), with a user migration for corrected profiles and upstream PRs where Decenza is canonical.

### Modified Capabilities
<!-- TBD when fleshed out. Likely none at spec level; this is data + tooling. -->

## Impact

- **Tool:** `tools/profile_sync.cpp` (3-way compare, reaprime JSON input).
- **Parser:** `src/profile/profile.cpp` (`settings_2a` prefer-regenerated-frames hardening).
- **Bundled data:** `resources/profiles/*.json` (content/dedup fixes, regeneration).
- **Migration:** profile-seeding path (retire/refresh corrected built-ins).
- **External:** PRs to tadelv/reaprime (`assets/defaultProfiles/`, `manifest.json`); Visualizer canonical JSON sourcing.
- **Docs:** the audit doc (in this change) + `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **Depends on:** `align-profile-json-with-reaprime` (Change 1) shipping first.
