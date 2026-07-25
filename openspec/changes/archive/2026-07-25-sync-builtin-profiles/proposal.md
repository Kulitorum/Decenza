> **STATUS: measured, scope reduced, ready to implement.** `align-profile-json-with-reaprime` (Change 1) merged 2026-07-25 as `5a1bc68a`, with `fix-de1app-profile-drift` alongside it. The audit this proposal called for has now been run, and it **inverted much of what follows** — see "What the measurement found" below before reading the rest.
>
> **Also read "Already delivered" and "Upstream: de1app issue #350".** Change 1 and the drift fix overtook part of this proposal's original scope; #350 is open upstream and constrains which de1app copies may be used as references at all.

## Why

The Decent community needs a profile to make the **same coffee in every app**. Change 1 (`align-profile-json-with-reaprime`) makes Decenza's profiles *readable* by reaprime; this change makes the built-in profiles *equivalent* in content, so importing a shared profile yields the identical extraction regardless of which app authored it.

## What the measurement found

The three counts this proposal originally quoted — ~63 common, ~14 differing in step count, ~50 with real value differences — were taken before Change 1 corrected built-in content, and the reaprime side had never been re-checked. They have now been reproduced against `resources/profiles/` (93 files) and `tadelv/reaprime@25bf9f25` `assets/defaultProfiles/` (70 files):

| | measured |
|---|---|
| Titles common to both apps | **63** |
| …already machine-equivalent | **52** |
| …differing in frame **count** | **8** |
| …differing in frame **values** | **3** |

The "~50 value differences" was an artefact of comparing encodings. Three conventions account for it and none changes the shot: 402 rows where Decenza omits `weight` and reaprime writes `"0.0"`, 228 rows where Decenza emits a no-op `limiter {value 0.00}` that reaprime omits, and ~50 where reaprime writes `""` on the axis the pump does not drive.

**All 11 divergences resolve in Decenza's favour**, each traceable to one of two upstream mechanisms: de1app's `save_profile` writing `advanced_shot` out of the *global* `::settings` array (so a `settings_2a`/`2b` file ships frames belonging to whichever profile was last loaded), and de1app issue #350 shadowing the A-Flow profiles. The two `settings_2c` cases are the check on that conclusion — advanced profiles involve no frame regeneration, so Decenza has no mechanism there to be accidentally right, and it reproduces de1app's frames exactly on both.

The 52 split further: 37 identical on every compared field, and 15 differing only in `beverage_type`, where reaprime's converter flattened de1app's `tea`/`tea_portafilter`/`filter` onto `pourover`. That is label loss, not a brew difference — de1app uses beverage type for display text only. `audit.md` carries the per-profile breakdown.

**Consequence: this change makes no content edits to Decenza's built-ins.** Nothing was found to correct.

**Since verified fixed.** reaprime landed `fix-legacy-profile-ingest`; re-running the comparison against their rebuilt corpus returns **64 of 64 equivalent, zero divergences** — the count rises from 63 because `A-Flow / default-light` now exists on both sides.

The reconciliation was still case-by-case, and the outcome is a finding rather than a rule — neither app is automatically authoritative, and re-running the comparison must re-establish this rather than assume it.

## What Changes

- **Per-profile audit doc** recording all 63 common profiles with verdict, cause and evidence, plus the comparison script that produced them so re-running is not a re-derivation.
- **Two `profile_sync` defects this work surfaced**, both concerning the de1app comparison: the plugin-over-base precedence that silently discards a disagreeing source, and `isAdvancedType()` treating `settings_2c2` as advanced where de1app's stop-target switches do not.
- **Hand the reaprime corrections upstream** and track de1app #350 — the one divergence neither side can close.
- **Convert existing LOCAL USER profiles to the canonical format — sequenced LAST.** Change 1 made every profile the app *emits* canonical (save, export, share, Visualizer upload all route through `Profile::toJsonObject()`), but files already on disk stay in the old numeric encoding until something re-saves them. That is not cosmetic: `DatabaseBackupManager` backs profiles up with a raw `copyDirectory()` (`databasebackupmanager.cpp:493`), so legacy-format files travel verbatim into backups and onto other devices, where a stricter reader (reaprime) rejects them for the missing `tank_temperature` / `target_volume_count_start`.
  - **Parity-gated, never blind.** A one-time pass re-saves each user profile only when `Profile::jsonParityErrors(original, converted)` reports zero loss; anything that would lose a key or drift a value is **skipped and logged**, not written. Verified lossless on a real user profile during Change 1 validation (0 keys lost, 0 value drift).
  - **Runs last, though the reason has weakened.** The original sequencing was so user files are touched exactly once, after any content reconciliation. There is now no content reconciliation, so the ordering costs nothing and the conversion stands on its own justification above.
  - **Scope note:** this converts *encoding*, never user-authored *values* — which is why it does not conflict with the standing rule against retro-rewriting user-set data.
- **NOT doing:** the serialization-format work (Change 1) and the `recipe` round-trip (Change 3, `preserve-recipe-visualizer-roundtrip`).

### Struck after the measurement

Each of these presupposed Decenza being wrong somewhere, or automating a question that is now answered. Removed with the reason, rather than silently dropped:

- ~~**Content + dedup fixes to Decenza built-ins.**~~ The audit found nothing warranting correction. The dedup collisions it did find (`Bug Bite Oolong`/`oolong dark`, `Blue Willow: Tsuyuhikari Sensha`/`Sencha`, `Chinese green`/`white tea`, `D-Flow / Q`/`Damian's Q`/`Test/profile_editor_demo`, `Damian's LRv2`/`Londonium`) are tea and test profiles where identical frames may well be intended; the last is frame-identical in reaprime too, so it is upstream duplication rather than ours. `Flow profile for milky drinks` and `…for straight espresso` are **not** byte-copies in Decenza today, contrary to this proposal's original claim.
- ~~**A one-time user migration** for corrected built-ins.~~ Nothing was corrected, so no user holds a stale copy needing re-delivery.
- ~~**Visualizer canonical-JSON sourcing.**~~ It existed to break ties the audit did not produce.
- ~~**Extend `profile_sync` to a 3-way compare.**~~ Descoped — see design D3. The comparison has been run and recorded, and reaprime's corpus is now `fix-legacy-profile-ingest`'s responsibility in their repo.
- ~~**Scope limit — A-Flow and D-Flow are OUT of the reaprime comparison.**~~ The premise was wrong. reaprime's A-Flow files are byte-faithful copies of de1app's stale 6-frame snapshot (issue #350), not the output of a broken editor, and D-Flow's one common profile compares equivalent. Replaced by classify-by-cause — see design D4.
- ~~**Upstream PRs to tadelv/reaprime.**~~ Superseded by an OpenSpec change authored in their repo (`fix-legacy-profile-ingest`), which fixes their converter rather than shipping them our files — so their next re-harvest from de1app cannot undo it.

## Already delivered — do not rebuild

Change 1 and `fix-de1app-profile-drift` landed together and took over part of this proposal's scope. Each item below is done, specified, and tested; treat it as foundation.

- **The `settings_2a` stale-`advanced_shot` hardening this proposal asked for is DONE.** `loadFromTclString()` regenerates simple-profile frames through `regenerateSimpleFrames()` rather than reading the stored array, matching de1app's own dispatch. Specified as "Simple profiles derive frames from their scalars" in `openspec/specs/de1app-profile-parity/spec.md`. The evidence is worth reading before touching lever profiles: de1app's legacy save writes `advanced_shot` out of the **global** `::settings` array, so 10 of the 12 stock simple profiles ship frames contradicting their own `espresso_temperature`, and five share one byte-identical pour-over frame list belonging to none of them. See the corollary section of `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **The A-Flow 6-vs-9-frame provenance question is SETTLED.** `Jan3kJ/A_Flow` (the plugin submodule) is the source; `de1plus/profiles/` holds a stale snapshot added 2025-09-03 in de1app commit `80eb34cc` "so they can be translated" and never refreshed, while the source updated twice after (`9ca39813`, `7784922b`). `profile_sync` already prefers the plugin copy. Do not re-open it by preferring the base copy — the task below hardens that precedence into an assertion that fails when the two sources disagree, which is not the same as reversing it.

## Upstream: de1app issue #350 — read before choosing reference sources

[decentespresso/de1app#350](https://github.com/decentespresso/de1app/issues/350) (filed 2026-07-25, **open**) carries three findings from the Change 1 work. Two of them constrain how this change sources its references; one is an open upstream decision this change must not pre-empt.

- **The A-Flow shadowing is now upstream, and the fix may invert our preference rule.** The issue documents exactly what "Already delivered" records above: `de1plus/profiles/` ships four A-Flow profiles at 6 frames, the `A_Flow` plugin ships all five at 9, and `check_profiles_exist` copies only when the file is *absent* — so the distribution copy wins forever and cannot self-correct on any plugin update. **But the plugin author's reply proposes the opposite resolution** to the issue's own preferred fix: rather than removing the four base copies, Jan3kJ offers to remove the defaults *from the plugin* and maintain all five in `de1plus/profiles/`, since A-Flow is now an always-active editor. Either resolution ends the shadowing; only one of them leaves the plugin as the canonical source. `tools/profile_sync.cpp` hard-codes "plugin copy wins" (`profile_sync.cpp:14`, `:289`) — correct today, wrong the moment that reply lands. **Do not treat the preference as settled data**: the source-of-truth directory must become a checked assumption that fails loudly, not a comment. That is the surviving piece of the descoped tool work, and it is the reason the plugin-precedence assertion stayed in scope when the reaprime leg did not.
- **The de1app reference is the git repo at `main`, never a downloaded release build.** `best_practice_light.tcl` in the `v1.46.1-113-g745c626e` build differs from the repo copy in six fields, three of which change the shot (`espresso_temperature` 92→96, `final_desired_shot_weight` 36→50, `final_desired_shot_volume_advanced_count_start` 2→1, the last of which is sent as `NumberOfPreinfuseFrames`). It cannot have come from de1app's own save path — it carries `water_temperature`, a key absent from `profile_vars`, so `save_profile` cannot emit it, plus float round-trip noise (`6.800000000000004`) and no `read_only` flag. 81 of the release's 82 `.tcl` files are byte-identical to the repo, so this is one stray file, not a packaging step. **Any audit row sourced from a release tarball is untrustworthy for this profile and unverified for the rest** — pin the comparison to `decentespresso/de1app@main` and record the commit in the audit doc.
- **`insert_preinfusion_pause` frame-count semantics are an open upstream decision — do not "fix" our side to match the apparent intent.** `de1plus/binary.tcl:995` reads `::setting(...)` (singular) where line 880 reads `::settings(...)`, and `ifexists` on a nonexistent array never returns 1, so the branch is dead: the pause frame *is* prepended but `NumberOfPreinfuseFrames` is *not* incremented, and the DE1 is told preinfusion ends a frame early. Decenza deliberately does not increment either, matching what the machine actually receives rather than what the code appears to intend. That is the right default while the issue is open, and it is a divergence this change may have to revisit — if de1app corrects line 995, every profile using the pause changes its header byte and our parity gate will flag it. Treat a de1app fix here as a scheduled input to the audit, not a surprise.

### Tooling that now exists and this change should use

- **A de1app oracle** that sources de1app's real `de1plus/profile.tcl` and calls *their* frame builders through their own dispatch — nothing reimplemented, because a reimplementation is one more thing that can drift. It found divergences the whole C++ suite had missed.
- **A type-aware profile-level scalar drift gate** in `tools/profile_sync.cpp`, plus the shared rule in `src/profile/de1apptclfields.h` so the importer and the gate cannot disagree about which of two spellings wins.
- **`Profile::reaprimeReadabilityErrors()`**, already run against every shipped built-in by `tests/tst_builtinprofileformat.cpp`.
- **The immutable golden corpus** at `tests/data/profiles_legacy/`, which fails on unreviewed built-in content change. This is what already gates the drift that matters — our own built-ins changing — and is why a second reaprime-facing regression corpus was descoped (design D5).

**Two gaps this tooling has, now tasked.** `profile_sync` cannot compare two `.tcl` sources at all: `Profile::frameDiffReport()` is `Profile`-vs-`Profile` and would work, but `De1AppTcl::compareScalars()` is tcl-vs-JSON by signature. That is why the tool saw the shadowed 6-frame A-Flow copy, discarded it by precedence, and reported success. Separately, `De1AppTcl::isAdvancedType()` treats `settings_2c2` as advanced, matching de1app's frame handling but not its stop-target switches.

## Capabilities

### New Capabilities
- `builtin-profile-sync`: Cross-app content equivalence of the bundled profile set — the machine-observable definition of "equivalent", the case-by-case reconciliation of Decenza's built-ins against reaprime with de1app as reference, and the rule that a divergence is classified by cause rather than suppressed by category. Also covers the parity-gated conversion of existing local user profiles to the canonical encoding.

### Modified Capabilities
<!-- None. The audit found no Decenza built-in needing correction, so no shipped
     behaviour changes. The user-profile format conversion changes encoding on disk,
     not behaviour, and `profile-json-interchange` already owns the canonical format. -->

## Impact

- **Tool:** `tools/profile_sync.cpp` — the plugin-over-base precedence becomes a loud assertion; `src/profile/de1apptclfields.h` for the `settings_2c2` question. The reaprime leg is **not** built (design D3).
- **Parser:** no change. The `settings_2a` prefer-regenerated-frames hardening originally listed here shipped in Change 1 — see "Already delivered".
- **Bundled data:** none. `resources/profiles/*.json` is unchanged — the audit found nothing to correct.
- **Migration:** the parity-gated user-profile format conversion only. The profile-seeding retire/refresh path is not touched, because no built-in was corrected.
- **External:** `fix-legacy-profile-ingest`, an OpenSpec change authored in the reaprime clone and tracked separately; de1app [#350](https://github.com/decentespresso/de1app/issues/350).
- **Docs:** the audit doc + its comparison script (in this change) + `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **Specs already owning adjacent behaviour** (read before adding requirements, to avoid restating them): `openspec/specs/de1app-profile-parity/spec.md` and `openspec/specs/profile-json-interchange/spec.md`.
- **Depends on:** `align-profile-json-with-reaprime` (Change 1) — **satisfied**, merged 2026-07-25 as `5a1bc68a`.
