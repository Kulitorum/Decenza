## Context

The proposal's question is "do the built-ins make the same coffee in every app?" — and it warned that its own three counts (~63 common, ~14 step-count differences, ~50 value differences) were measured before Change 1 corrected built-in content, so they had to be reproduced before anything was planned against them.

**They have been reproduced, and the answer is much narrower than the proposal feared.** Measured 2026-07-25 against `resources/profiles/` (93 files) and `tadelv/reaprime@25bf9f25` `assets/defaultProfiles/` (70 files):

| | count |
|---|---|
| Titles common to both apps | **63** |
| …machine-equivalent already | **52** |
| …differing in frame **values** | **3** |
| …differing in frame **count** | **8** |

The "~50 have real value differences" figure was an artefact of comparing encodings. Three divergence classes account for it and none of them changes the shot:

- **402 rows** where Decenza omits `weight` and reaprime writes `"0.0"`/`"0"`.
- **228 rows** where Decenza emits `limiter {value 0.00 range 0.60}` and reaprime omits the block. A zero-value limiter is a no-op.
- **~50 rows** where reaprime writes `""` for the axis the frame's pump does not drive and Decenza writes `0.00`. The DE1 ignores that axis; `profile_sync.cpp` already encodes this rule for the de1app leg.

Strip those three and 52 of 63 are already identical to the machine.

### Every one of the 11 divergences resolves in Decenza's favour

This is the finding that reshapes the change. Each divergence was traced to its de1app source, and there is no case where Decenza is the less faithful side:

| Profile | de1app type | Decenza | reaprime | Verdict |
|---|---|---|---|---|
| `7g basket` | `settings_2a` | 5 (regenerated) | 4 (stored) | Decenza |
| `Classic Italian espresso` | `settings_2a` | 4 (regenerated) | 5 (stored) | Decenza |
| `Preinfuse then 45ml of water` | `settings_2b` | 3 (regenerated) | 6 (stored) | Decenza |
| `Default` | `settings_2a` | 90/88 flat | 88→**75**→**54** | Decenza |
| `Gentle and sweet` | `settings_2a` | 88 flat | 88→**78.5**→**67** | Decenza |
| `A-Flow / default-{dark,medium,very-dark,like-dflow}` | plugin | 9 frames | 6 frames | Decenza |
| `Advanced spring lever` | `settings_2c` | 5, matches de1app exactly | 5, a **different profile** | Decenza |
| `Cleaning/Forward Flush x5` | `settings_2c` | 10, matches de1app exactly | 9 | Decenza |

Two upstream mechanisms explain all of them, and both are already documented:

1. **de1app's stored `advanced_shot` contradicts its own simple profiles.** `save_profile` writes that array out of the *global* `::settings`, so a `settings_2a`/`2b` file ships frames belonging to whatever profile was last loaded. reaprime harvested those stored frames; Decenza regenerates from the scalars (Change 1's "Simple profiles derive frames from their scalars"). Brew temperatures of **75 °C and 54 °C** in reaprime's `Default` are the fingerprint — no espresso profile declines to 54 °C, and de1app's own `espresso_temperature` for that file is 90.0.
2. **de1app issue #350's A-Flow shadowing.** reaprime's A-Flow copies are the stale 6-frame distribution snapshot that permanently shadows the plugin's 9-frame source. reaprime is downstream of that bug, not independently wrong.

The two `settings_2c` (genuinely advanced) cases are the strongest evidence, because there the stored frames *are* authoritative and no regeneration is involved: Decenza reproduces de1app's frames exactly, while reaprime's `Advanced spring lever` is a wholly different profile (`pressure limit`/`flow limit` frames that appear nowhere in de1app's file) and its `Cleaning/Forward Flush x5` has dropped the `Pressure rise 1 start` frame.

**Consequence: this change ships no content edits to Decenza's built-ins.** The proposal's "content + dedup fixes to Decenza built-ins", its "one-time user migration so users who imported a now-corrected built-in receive the fixed version", and its Visualizer-sourcing dependency all presupposed Decenza being wrong somewhere. Nothing was found to correct, so there is nothing to migrate and nothing to source. What remains is a comparison tool, a regression gate, upstream PRs, and the user-profile format conversion.

## Goals / Non-Goals

**Goals:**
- Land the audit as a durable document with per-profile verdicts and their evidence.
- Fix the two `profile_sync` defects this work surfaced, both concerning the de1app comparison.
- Hand the reaprime corrections upstream and track de1app #350, the one divergence neither side can close.
- Convert existing local user profiles to the canonical encoding, parity-gated.

**Descoped after the measurement — see D3 and D5 for the reasoning that no longer applies:**
- ~~Extend `tools/profile_sync.cpp` with a reaprime leg.~~ The comparison has been run and its answer recorded; the corpus it would compare against is now `fix-legacy-profile-ingest`'s responsibility in the reaprime repo.
- ~~Pin the finding with a regression test over reaprime equivalence.~~ It would encode "these 11 diverge" exactly as reaprime fixes all 11.

**Non-Goals:**
- Content edits to `resources/profiles/*.json`. The audit found none warranted; if that changes, it is a new change, not a silent widening of this one.
- The built-in retire/refresh user migration. Nothing was corrected, so nothing needs re-delivering.
- Visualizer canonical-JSON sourcing. It existed to break ties the audit did not produce.
- Re-litigating the de1app leg. `de1app-profile-parity` owns it and `tst_tclimport` gates it.
- Fixing de1app or reaprime in this repo. Upstream work is PRs, tracked here but not gated on.

## Decisions

### D1 — Equivalence is machine-observable, not textual

The comparison answers "does the DE1 receive the same instructions?" Two profiles are equivalent when, after normalisation, their frame lists agree:

- absent, `""` and `0` are the same value;
- the axis a frame's pump does not drive is not compared (the DE1 ignores it);
- a `limiter` with `value == 0` equals an absent `limiter`;
- numbers compare numerically within 1e-3, never as strings (`"8.00"` == `"8.0"`);
- `exit` compares as (type, condition, value) with the same numeric rule.

*Alternative rejected:* structural JSON diff. It is what produces "50 of 63 differ" and buries three real findings under 630 rows of formatting.

### D2 — Join on `title`, never on filename

Decenza derives filenames through `Profile::titleToFilename()` (`80_s_espresso.json`); reaprime keeps de1app's export names (`80s_Espresso.json`, `A-Flow____default-dark.json`). Titles are byte-identical across all 63 common profiles, including punctuated ones (`A-Flow / default-dark`, `Cleaning/Forward Flush x5`). Filename matching would have reported 63 false "missing" rows.

*Consequence:* the tool must report title collisions as an error. Decenza currently has none (93 files, 93 distinct titles); reaprime has none either.

### D3 — The reaprime leg extends the existing tool; it does not fork it — **DESCOPED**

> **Not built.** The measurement it would automate is done and recorded in the audit, and reaprime's corpus is now `fix-legacy-profile-ingest`'s responsibility. Kept because the reasoning still binds *if* the leg is ever built: it must extend the existing comparison rather than fork it, and it must stay report-only. Re-running the comparison after reaprime lands is a manual step with throwaway scripts; that cost is accepted knowingly.

`profile_sync.cpp` already holds the pieces: `Profile::frameDiffReport()` for frames, `De1AppTcl::compareScalars()` for scalars, the de1app oracle for provenance, and the uncovered-key aggregation that makes a narrowed comparison fail loudly. The reaprime leg adds a third source directory and reuses all of it.

A third source changes the tool's shape in one way worth stating: today `--sync` means "pull de1app content into the built-ins", a direction that is safe because a built-in is *supposed to be* its de1app profile. **No equivalent write direction exists for reaprime.** The reaprime leg is report-only. Given D1's finding that Decenza is correct in all 11 cases, a `--sync-from-reaprime` would be a tool for corrupting the corpus.

*Alternative rejected:* a standalone `reaprime_compare` tool. Two comparisons that disagree about what "equivalent" means is the failure the scalar-field map (`de1apptclfields.h`) was introduced to prevent.

### D4 — Drop the blanket A-Flow / D-Flow exclusion; replace it with a provenance note

The proposal and the spec stub exclude A-Flow and D-Flow from the reaprime comparison on the grounds that "reaprime is believed broken for those two editor types." **The measurement does not support that rationale.** reaprime's A-Flow files are not the output of a broken editor — they are byte-faithful copies of de1app's stale 6-frame distribution snapshot, i.e. exactly de1app issue #350. And D-Flow is not affected at all: only `D-Flow / default` is common to both apps, and it compares **machine-equivalent** today.

Excluding them would suppress the clearest signal the comparison produces, and would have hidden the fact that the A-Flow gap is an upstream bug with a filed issue and a known fix.

**Replacement rule:** compare everything; classify each divergence by *cause*, and let a known-upstream cause be a reported category rather than a suppressed one. A-Flow's four rows are classified `upstream/de1app#350` and are not queued as Decenza defects — the same practical outcome the exclusion wanted, without the false premise or the blind spot.

This contradicts the delta spec as written, so the spec is updated in the same change (see tasks).

### D5 — The gate is a test over checked-in expectations, not a live comparison — **DESCOPED**

> **Not built.** The reasoning below is sound about *how* to gate without depending on another project's HEAD, but it assumed the verdicts were worth gating at all. They are not: our own built-ins are already covered by the immutable golden corpus at `tests/data/profiles_legacy/`, and a fixture asserting "these 11 diverge from reaprime" would go red as reaprime fixes all 11 — regenerated on their release schedule rather than ours. Regression cover for the reaprime corpus belongs in `fix-legacy-profile-ingest`, which has it. Accepted cost: no automated signal if reaprime later regresses.

CI cannot clone reaprime, and pinning a submodule to another app's repo to run a test is a supply-chain and cache-budget cost this repo does not need (`project_ci_cache_budget_tight`). Instead:

- The audit records the 63 verdicts as data in `tests/data/profiles_reaprime_verdicts.json` — title, verdict, cause, and for the 11 the specific divergence.
- A test asserts that every profile marked `equivalent` still normalises to the frames recorded alongside it, and that the 11 still diverge in exactly the recorded way.
- The comparison against a live reaprime checkout stays a developer command, run when reaprime moves.

This catches the regression that matters (a Decenza built-in drifting) without making the build depend on another project's HEAD. It is the same shape as the immutable golden corpus in `tests/data/profiles_legacy/`.

### D6 — Upgrade on load, not in a one-time pass

Now the only Decenza-side behaviour change in the flight. Re-save a user profile through `Profile::toJsonObject()` only when `Profile::jsonParityErrors(original, converted)` is empty; skip and log otherwise. Converts encoding, never values — which is what keeps it clear of the standing rule against retro-rewriting user-set data.

**A one-time startup pass was the original plan and it cannot be correct.** It assumes the set of files is fixed at the moment the pass runs. It is not: a user drops profiles into the directory whenever they like — sideloaded, restored from a backup, synced from another device, written by another tool. Any pass that marks itself complete converts what happened to be present that day and silently ignores everything that arrives later, which is precisely the population most likely to be legacy-encoded. The completion flag is not a detail of that design, it *is* the bug.

Upgrading when a profile is loaded has none of that: it covers arrivals at any time, needs no flag, and never touches a profile the user does not use.

**Hook at `ProfileManager::loadProfile`, not `Profile::loadFromFile`.** The latter looks like the natural chokepoint and is the wrong one — 28 call sites outside `profile.cpp` read from `:/profiles/` resources, backup staging directories, migration temp files and importer scratch paths. A write-back there would attempt read-only resources and rewrite files that are not the user's library. `loadProfile`'s 4-tier resolve already distinguishes exactly the tiers that matter: ProfileStorage (SAF), `userProfilesPath()`, `downloadedProfilesPath()` are writable; `:/profiles/` is not.

The profile *list* does not go through either path — it reads raw JSON for metadata via `extractProfileMeta`. So listing a library does not trigger conversion, and "on load" means what it says.

**Two consequences worth stating rather than discovering later.**

*Writes must be atomic.* `Profile::saveToFile` opens `WriteOnly`, truncating in place. For git-tracked built-ins that is survivable; for rewriting a user's profile purely to reformat it, a crash or a full disk mid-write destroys a file that was previously fine. `QSaveFile` fixes it for every writer, not just this one, which is why the fix goes in `saveToFile` rather than at the call site.

*Idempotence is required, not nice-to-have.* Without a "already canonical" check the file would be rewritten on every activation, churning mtime forever. Compare the parsed objects — an already-converted profile re-serialises to an equal object and is skipped.

*Threading:* the write is synchronous on the main thread, matching the surrounding code — `loadProfile` already does synchronous `QFile::exists` and `loadFromFile`, and `saveProfile` already writes synchronously from QML. Introducing a background thread for one ~4 KB write inside an otherwise-synchronous function would add a race (two loads scheduling overlapping writes) for no measurable gain. Noting it because the project rule is otherwise absolute; if that is the wrong call, the fix is to move the whole resolve off-thread rather than just this write.

## Risks / Trade-offs

**de1app #350 is resolved the other way → our "plugin is canonical" rule inverts.** The plugin author has offered to delete the defaults from the plugin and maintain all five in `de1plus/profiles/`, which is the opposite of the issue's preferred fix. `profile_sync.cpp:289` hard-codes "plugin copy wins."
→ *Mitigation:* make the preference an assertion that fails loudly when both sources exist and disagree, rather than a silent precedence rule. Either upstream resolution then surfaces as a tool error naming both paths, instead of silently selecting a stale file.

**reaprime re-harvests and the 11 change under us.** The verdict file is a snapshot of `25bf9f25`.
→ *Mitigation:* record the reaprime commit in the verdict file and report it in the tool banner. A verdict file naming a commit that is no longer HEAD is a prompt to re-run, not a stale gate.

**The audit's conclusion is "we were right about everything," which is the shape of a conclusion that did not look hard enough.**
→ *Mitigation:* the two `settings_2c` cases are the check on this, and they were chosen deliberately — advanced profiles involve no regeneration, so Decenza has no mechanism there to be accidentally right. Decenza matches de1app's stored frames exactly on both. The verdict file records the de1app evidence per profile so the claim is auditable rather than asserted.

**Upstreaming 11 profiles to reaprime is a large unsolicited PR touching another project's data.**
→ *Mitigation:* lead with the mechanism (de1app writes stored frames from the global settings array; #350 shadows A-Flow), not with a diff. The 75 °C/54 °C temperatures in their `Default` demonstrate it in one line. Split A-Flow (blocked on #350) from the simple-profile regeneration (actionable now).

**`insert_preinfusion_pause` semantics may move upstream** (de1app `binary.tcl:995`, the dead `::setting` branch). A fix there changes `NumberOfPreinfuseFrames` for every profile using the pause.
→ *Mitigation:* out of scope here; recorded in the proposal as a scheduled input. No common built-in currently sets it, so today it changes none of the 63.

## Migration Plan

No data migration. The built-ins are unchanged, so no user holds a corrected copy that needs re-delivering.

The one on-disk change is the user-profile format conversion (D6): a one-time pass at startup, parity-gated per file, skipping and logging anything lossy. Rollback is to stop running the pass — converted files remain readable by both encodings, which is what Change 1 established.

## Open Questions

1. **Do we open the reaprime PRs from this change, or file an issue first?** Their #242 shows they know the corpus has provenance problems; an issue describing the mechanism may be more useful than 11 file diffs. *Recommendation: issue first, PR on request.*
2. **Does the A-Flow PR wait on de1app #350?** Sending reaprime 9-frame files while de1app still ships the 6-frame shadow means they may re-harvest the stale copies again. *Recommendation: yes, wait, and say so in the issue.*
3. **Should `Damian's LRv2` / `Londonium` be deduplicated?** They are frame-identical in *both* apps, so it is a shared upstream duplication rather than a Decenza defect. Four further Decenza-only collisions exist (`Bug Bite Oolong`/`oolong dark`, `Blue Willow: Tsuyuhikari Sensha`/`Sencha`, `Chinese green`/`white tea`, and `D-Flow / Q`/`Damian's Q`/`Test/profile_editor_demo`). All are tea/test profiles where identical frames may well be intended. *Needs a user decision; not assumed either way.*
