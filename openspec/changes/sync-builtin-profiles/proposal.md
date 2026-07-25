> **STATUS: UNBLOCKED, needs design + tasks.** `align-profile-json-with-reaprime` (Change 1) merged 2026-07-25 as `5a1bc68a`, and `fix-de1app-profile-drift` merged with it. `design.md` and `tasks.md` do not exist yet — create them with `/opsx:continue` before implementing.
>
> **Read "Already delivered" below first.** Change 1 and the drift fix overtook part of this proposal's scope and settled one of its open questions, and the profile counts quoted under "Why" were measured *before* built-in content was corrected, so they no longer describe the current tree.

## Why

The Decent community needs a profile to make the **same coffee in every app**. Change 1 (`align-profile-json-with-reaprime`) makes Decenza's profiles *readable* by reaprime; this change makes the built-in profiles *equivalent* in content, so importing a shared profile yields the identical extraction regardless of which app authored it.

The ~63 built-in profiles common to Decenza and reaprime are **not** identical today: ~14 differ in step count (e.g. A-Flow 6 vs 9 frames), and ~50 have real value differences, on top of encoding/omitted-zero noise.

> **These three counts are stale — re-measure before planning against them.** They were taken before Change 1, which corrected built-in content (including three invented values in the frame generators and the simple-profile frame derivation). The Decenza side of the comparison has moved since; the reaprime side has not been re-checked at all. The first task of this change is to reproduce the numbers, not to act on them. Neither side is automatically authoritative: reaprime's set was pulled from Visualizer + de1app copy-exports and is documented (their issue #242 curation) to have contained stale notes, duplicate collisions, and **lever-profile corruption** (importing de1app's stale `advanced_shot` instead of the `settings_2a` intent). Decenza is likely the more faithful side for several (A-Flow 9-frame matches de1app's editor; levers), while reaprime may be cleaner for others. So sync is **bidirectional and case-by-case**, not a one-way overwrite.

## What Changes

- **Extend `tools/profile_sync.cpp` to a 3-way compare** — de1app-Tcl ↔ Decenza-JSON ↔ reaprime `assets/defaultProfiles` (JSON) — to surface every divergence in one report and drive the audit.
- **Scope limit — A-Flow and D-Flow are OUT of the reaprime comparison.** reaprime is believed broken for those two editor types, so differences against it there are not evidence that Decenza is wrong and are not to be reconciled. They remain in scope against **de1app**, where Change 1 already holds them to parity. This is what removes "A-Flow 6 vs 9 frames" from the work rather than answering it.
- **Per-profile audit doc** classifying each common profile keep / metadata-fix / content-fix / dedup / needs-decision. Divergent profiles are reviewed **case by case** with the user — no blanket "de1app wins" or "reaprime wins" rule. Suspect heuristic: any `settings_2a` profile carrying explicit frames is flagged until reviewed.
- **Content + dedup fixes to Decenza built-ins** where warranted (`resources/profiles/*.json`), plus a check for the same collisions reaprime found (Sencha/Sensha, Chinese-green/white-tea, Bug-Bite/oolong-dark, milky=straight byte-copies).
- **A one-time user migration** so users who imported a now-corrected built-in receive the fixed version (Decenza's equivalent of reaprime's M1 metadata-refresh / M2 retire-list).
- **Upstream PRs to tadelv/reaprime** for profiles where Decenza is the more faithful side (A-Flow 9-frame; any lever/param corrections), updating their `assets/defaultProfiles/` + `manifest.json`.
- **Sourcing dependency:** obtain Visualizer canonical JSON for genuinely-disputed profiles where neither app's frames are trustworthy (reaprime was blocked on this too — lever re-port, milky differentiation).
- **Convert existing LOCAL USER profiles to the canonical format — sequenced LAST, after the content reconciliation above.** Change 1 made every profile the app *emits* canonical (save, export, share, Visualizer upload all route through `Profile::toJsonObject()`), but files already on disk stay in the old numeric encoding until something re-saves them. That is not cosmetic: `DatabaseBackupManager` backs profiles up with a raw `copyDirectory()` (`databasebackupmanager.cpp:493`), so legacy-format files travel verbatim into backups and onto other devices, where a stricter reader (reaprime) rejects them for the missing `tank_temperature` / `target_volume_count_start`.
  - **Parity-gated, never blind.** A one-time pass re-saves each user profile only when `Profile::jsonParityErrors(original, converted)` reports zero loss; anything that would lose a key or drift a value is **skipped and logged**, not written. Verified lossless on a real user profile during Change 1 validation (0 keys lost, 0 value drift).
  - **Runs after the content work, deliberately.** Reconciling built-in content first means user files are touched exactly once; migrating first would churn them again if the audit changes the format or surfaces a serialization bug.
  - **Scope note:** this converts *encoding*, never user-authored *values* — which is why it does not conflict with the standing rule against retro-rewriting user-set data.
- **NOT doing:** the serialization-format work (Change 1) and the `recipe` round-trip (Change 3, `preserve-recipe-visualizer-roundtrip`).

## Already delivered — do not rebuild

Change 1 and `fix-de1app-profile-drift` landed together and took over part of this proposal's scope. Each item below is done, specified, and tested; treat it as foundation.

- **The `settings_2a` stale-`advanced_shot` hardening this proposal asked for is DONE.** `loadFromTclString()` regenerates simple-profile frames through `regenerateSimpleFrames()` rather than reading the stored array, matching de1app's own dispatch. Specified as "Simple profiles derive frames from their scalars" in `openspec/specs/de1app-profile-parity/spec.md`. The evidence is worth reading before touching lever profiles: de1app's legacy save writes `advanced_shot` out of the **global** `::settings` array, so 10 of the 12 stock simple profiles ship frames contradicting their own `espresso_temperature`, and five share one byte-identical pour-over frame list belonging to none of them. See the corollary section of `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **The A-Flow 6-vs-9-frame provenance question is SETTLED.** `Jan3kJ/A_Flow` (the plugin submodule) is the source; `de1plus/profiles/` holds a stale snapshot added 2025-09-03 in de1app commit `80eb34cc` "so they can be translated" and never refreshed, while the source updated twice after (`9ca39813`, `7784922b`). `profile_sync` already prefers the plugin copy. Do not re-open it by preferring the base copy.

### Tooling that now exists and this change should use

- **A de1app oracle** that sources de1app's real `de1plus/profile.tcl` and calls *their* frame builders through their own dispatch — nothing reimplemented, because a reimplementation is one more thing that can drift. It found divergences the whole C++ suite had missed.
- **A type-aware profile-level scalar drift gate** in `tools/profile_sync.cpp`, plus the shared rule in `src/profile/de1apptclfields.h` so the importer and the gate cannot disagree about which of two spellings wins. Extend this for the reaprime leg rather than writing a third comparison.
- **`Profile::reaprimeReadabilityErrors()`**, already run against every shipped built-in by `tests/tst_builtinprofileformat.cpp`.
- **The immutable golden corpus** at `tests/data/profiles_legacy/`, which fails on unreviewed built-in content change.

**Frames are still uncompared at the scalar gate's level.** The de1app oracle covers frames against de1app; there is no equivalent for reaprime yet. That gap is this change's core work.

## Capabilities

### New Capabilities
- `builtin-profile-sync`: Cross-app content equivalence of the bundled profile set — a tooling-driven, case-by-case reconciliation of Decenza's built-ins against reaprime (and de1app/Visualizer as references), with a user migration for corrected profiles and upstream PRs where Decenza is canonical. Also covers the parity-gated conversion of existing local user profiles to the canonical encoding, sequenced after the content reconciliation.

### Modified Capabilities
<!-- TBD when fleshed out. Likely none at spec level; this is data + tooling. -->

## Impact

- **Tool:** `tools/profile_sync.cpp` — extend the existing type-aware gate and de1app oracle with the reaprime leg (`assets/defaultProfiles` JSON); do not add a parallel comparison.
- **Parser:** no change expected. The `settings_2a` prefer-regenerated-frames hardening originally listed here shipped in Change 1 — see "Already delivered".
- **Bundled data:** `resources/profiles/*.json` (content/dedup fixes, regeneration).
- **Migration:** profile-seeding path (retire/refresh corrected built-ins), plus the parity-gated user-profile format conversion (sequenced last).
- **External:** PRs to tadelv/reaprime (`assets/defaultProfiles/`, `manifest.json`); Visualizer canonical JSON sourcing.
- **Docs:** the audit doc (in this change) + `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **Specs already owning adjacent behaviour** (read before adding requirements, to avoid restating them): `openspec/specs/de1app-profile-parity/spec.md` and `openspec/specs/profile-json-interchange/spec.md`.
- **Depends on:** `align-profile-json-with-reaprime` (Change 1) — **satisfied**, merged 2026-07-25 as `5a1bc68a`.
