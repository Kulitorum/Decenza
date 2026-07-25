# Built-in profile audit: Decenza ↔ reaprime

**Question:** do the bundled profiles the two apps share make the same coffee?

**Answer:** at the baseline, 52 of 63 did. The other 11 diverged and every one resolved in Decenza's favour, each traceable to an upstream de1app mechanism rather than to either app's own logic. A further 15 differed only in how they were labelled. **All of it is now fixed** — `fix-legacy-profile-ingest` landed in reaprime and the comparison returns 64 of 64 equivalent.

## Method

Equivalence is machine-observable, per the spec requirement *"Cross-app profile equivalence is machine-observable"*: absent/`""`/`0` are one value, the axis a frame's pump does not drive is ignored (the DE1 ignores it too), a zero-value limiter equals an absent one, numbers compare numerically, and the stop-target scalars are part of the comparison because two copies can carry identical frames and still stop at different weights.

Profiles are joined on `title`, never filename — Decenza derives filenames through `Profile::titleToFilename()` (`80_s_espresso.json`) while reaprime keeps de1app's export names (`80s_Espresso.json`). Titles are byte-identical across every common profile; filename matching would report 63 false misses.

Re-run with the script beside this file:

```bash
git -C <reaprime> archive <commit> assets/defaultProfiles | tar -x -C /tmp/rea
python3 compare_reaprime.py resources/profiles /tmp/rea/assets/defaultProfiles
```

Pin the reaprime side to a commit. Running against a working tree makes the numbers unreproducible, which matters here because that corpus changed mid-audit.

## Baseline — `tadelv/reaprime@25bf9f25`, Decenza `main` (93 built-ins)

| | count |
|---|---|
| Titles common to both apps | **63** |
| …fully equivalent | **37** |
| …differing only in `beverage_type` | **15** |
| …differing in frame **count** | **8** |
| …differing in frame **values** | **3** |
| Decenza-only titles | 30 |
| reaprime-only titles | 7 |

### The encoding noise this replaces

A structural JSON diff reports 55 of the 63 as differing. Three conventions account for essentially all of it, and none changes what the machine does:

| class | rows | |
|---|---|---|
| omitted zero `weight` | 402 | Decenza omits the key; reaprime writes `"0.0"` or `"0"` |
| zero-value `limiter` | 231 | Decenza emits `{value 0.00, range 0.60}`; reaprime omits the block. A zero-value limiter is a no-op |
| inactive-axis encoding | 27 | reaprime writes `""` for the axis the pump does not drive; Decenza writes `0.00` |

This is why the definition is load-bearing rather than stylistic. The original proposal's "~50 have real value differences" was this noise; the real number is 11.

## The 11 brew-affecting divergences

Every one resolves in Decenza's favour. `de1app type` is `settings_profile_type` from de1app's own `.tcl`.

| Profile | de1app type | Decenza | reaprime | cause |
|---|---|---|---|---|
| `7g basket` | `settings_2a` | 5 frames, stop 35 g | 4 frames, stop 36 g | stale `advanced_shot` + `_advanced` target |
| `Classic Italian espresso` | `settings_2a` | 4 frames, stop 36 g | 5 frames, **stop 60 g** | stale `advanced_shot` + `_advanced` target |
| `Preinfuse then 45ml of water` | `settings_2b` | 3 frames, stop 36 g | 6 frames, **no stop** | stale `advanced_shot` + `_advanced` target |
| `Default` | `settings_2a` | 90/88 flat | 88 → **75** → **54** | stale `advanced_shot` |
| `Gentle and sweet` | `settings_2a` | 88 flat | 88 → **78.5** → **67** | stale `advanced_shot` |
| `A-Flow / default-dark` | `settings_2c` | 9 frames | 6 frames | de1app #350 |
| `A-Flow / default-medium` | `settings_2c` | 9 frames | 6 frames | de1app #350 |
| `A-Flow / default-very-dark` | `settings_2c` | 9 frames | 6 frames | de1app #350 |
| `A-Flow / default-like-dflow` | `settings_2c` | 9 frames | 6 frames | de1app #350 |
| `Advanced spring lever` | `settings_2c` | de1app's, 88 °C | a Weiss variant, 90 °C | wrong profile in the slot |
| `Cleaning/Forward Flush x5` | `settings_2c` | 10 frames | 9 frames | stale harvest |

`A-Flow / default-light` is absent from reaprime entirely — a twelfth case that a comparison over shared titles cannot see, found by listing what each side ships.

### Cause 1 — de1app's stored `advanced_shot` contradicts its own simple profiles

`save_profile` writes that array out of the **global** `::settings`, so a `settings_2a`/`2b` file ships whatever frames were loaded when it was last saved. de1app never reads them back for those types — it regenerates from the scalars at load time, which is what Decenza does (`regenerateSimpleFrames()`, specified in `de1app-profile-parity` as *"Simple profiles derive frames from their scalars"*). reaprime's converter copied them verbatim.

The fingerprint is unmistakable: reaprime's `Default` runs frames at **75 °C and 54 °C** while de1app's `default.tcl` declares `espresso_temperature 90.0`. No espresso profile declines to 54 °C.

The same mistake extends to the stop targets. de1app carries two spellings and picks between them by profile type — `de1plus/device_scale.tcl:1322` for weight and `de1plus/de1_de1.tcl:862` for volume, both `settings_2c { ...advanced } default { ...plain }`. Reading `_advanced` unconditionally is why `Classic Italian espresso` stopped at 60 g instead of 36 g, and why two others had no stop at all. Decenza's rule lives in `src/profile/de1apptclfields.h` with the same de1app citations.

### Cause 2 — de1app issue #350 shadows the A-Flow profiles

`de1plus/profiles/` ships four A-Flow profiles at 6 frames; the `A_Flow` plugin submodule ships all five at 9. The plugin seeds only when the file is *absent*, so the distribution copy wins permanently and cannot self-correct. reaprime harvested from the shadowed directory.

Verified directly: reaprime's tool run against `de1plus/profiles/A-Flow____default-dark.tcl` reproduces their shipped 6-frame file byte-identically; against `de1plus/plugins/A_Flow/profiles/` it yields 9.

### Cause 3 — `Advanced spring lever` held a different profile

de1app's is `settings_2c`, authored `Decent`, 5 frames at 88 °C, opening `2s infuse` and closing `maintain flow`. reaprime's was authored **John Weiss**, 5 frames at 90 °C, opening `infuse` and closing `flow limit`, with an extra `pressure limit` frame.

Those are the frame names of a *different* de1app profile — `Weiss advanced spring lever`, which both apps already shipped correctly. So reaprime carried Weiss twice and de1app's `Advanced spring lever` not at all: a collision, not a curation choice.

## The 15 label-only differences

`Filter 2.0`, `Filter3` (`filter`) and thirteen tea profiles — twelve `tea_portafilter` plus `Tea/in a basket` (`tea`) — all arrived in reaprime as `pourover`. Their converter mapped three de1app types onto one because its `BeverageType` enum held only five values.

de1app has all of them: its profiles use `tea_portafilter` (15 files), `pourover` (7), `filter` (2) and `tea` (1), and `tea_portafilter` is formally declared in `de1plus/app_metadata.tcl`. No evidence was found that de1app *brews* differently on beverage type — its uses are display text and the shot-file label — so this was information loss rather than a wrong shot.

## The check on this audit's own conclusion

An audit concluding "we were right about everything" is the shape of an audit that did not look hard enough. The two `settings_2c` cases are the deliberate control, and they were chosen because **advanced profiles involve no frame regeneration** — de1app's stored frames are authoritative for that type, so Decenza has no mechanism there to be accidentally right. If our correctness were an artefact of the regeneration path, these are where it would fail.

- **`Advanced spring lever`** — Decenza reproduces de1app's 5 frames on every compared field, plus `target_weight` 32, `target_volume` 0, `target_volume_count_start` 0 and author `Decent`. reaprime's matched neither de1app profile.
- **`Cleaning/Forward Flush x5`** — Decenza reproduces de1app's 10 frames on every compared field. The only differences are float round-trip noise in de1app's own file (`pressure 9.999999999999993` → `10.00`, `-5.717648576819556e-15` → `0.00`), which are inside any tolerance. reaprime had 9, having lost `Pressure rise 1 start`.

Both pass. The conclusion is not an artefact of the derivation path.

A second, weaker control: the reaprime-side claims were each verified by *running their own tool* against de1app rather than by inspection, and three shipped files reproduced byte-identically. That rules out our reading of their corpus being the thing that was wrong.

## Duplicate frame content

Not divergences, recorded because the original proposal asked for a dedup check and because the answer differs from what it assumed.

| group | note |
|---|---|
| `Damian's LRv2` = `Londonium` | frame-identical in **both** apps — upstream duplication from de1app, not ours |
| `Tea portafilter/Bug Bite Oolong` = `Tea portafilter/oolong dark` | Decenza-only pair |
| `Tea portafilter/Blue Willow: Tsuyuhikari Sensha` = `Tea portafilter/Sencha` | Decenza-only pair |
| `Tea portafilter/Chinese green` = `Tea portafilter/white tea` | Decenza-only pair |
| `D-Flow / Q` = `Damian's Q` = `Test/profile_editor_demo` | Decenza-only trio |

All are tea or test profiles where identical frames may well be intended; none was changed. **The proposal's claim that `Flow profile for milky drinks` and `…for straight espresso` are byte-copies is false** for Decenza today — they differ, and both compare equivalent to their reaprime counterparts.

## Current state — verified after `fix-legacy-profile-ingest`

Re-run against reaprime's rebuilt corpus:

```
Decenza titles : 93
reaprime titles: 71
common         : 64   (Decenza-only 29, reaprime-only 7)
VERDICTS: {'equivalent': 64}
```

**64 of 64 equivalent, zero divergences.** All 11 brew-affecting cases and all 15 label-only cases are closed, and `A-Flow / default-light` now exists on both sides — which is why the common count rose from 63 to 64.

The encoding-class counts persist (428 / 226 / 20 rows) and are expected to: the two apps still write zeroes and inactive axes differently, and nothing about that reaches the machine.

## What remains open

**de1app itself is still divergent on A-Flow, and neither change can close it.** The shadowing means a de1app user's data directory holds the 6-frame copies and cannot self-correct, so de1app brews 6 frames where Decenza and reaprime now brew 9. Only [decentespresso/de1app#350](https://github.com/decentespresso/de1app/issues/350) fixes that.

**`insert_preinfusion_pause` is an open upstream decision.** `de1plus/binary.tcl:995` reads `::setting` (singular) where line 880 reads `::settings`, so the branch is dead and `NumberOfPreinfuseFrames` is not incremented when the pause frame is prepended. Decenza matches what the machine actually receives rather than the apparent intent. No common built-in sets the flag today, so it changes none of the 64 — but a de1app fix would change the header byte for every profile using it.

**Correction to #350's own second finding.** That issue cites float round-trip noise as evidence the released `best_practice_light.tcl` was not written by de1app's save path. It does not support that: de1app's own repo profiles carry the same noise, as `Cleaning_forward_flush_x5.tcl` shows above. The `water_temperature` argument — the key is absent from `profile_vars`, so `save_profile` cannot emit it — still stands and is the one to lead with.
