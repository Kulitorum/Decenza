## Context

`ShotAnalysis::analyzeFlowVsGoal` (src/ai/shotanalysis.cpp:314) runs two independent arms over an espresso shot's pour window:

- **Arm 1** averages actual flow against the commanded flow goal during flow-mode phases where the goal is stationary (15 % rel across ±0.75 s) and above a minimum threshold (0.3 mL/s). Reports `delta = mean_actual - mean_goal`; `|delta| > 0.4 mL/s` fires the "grind too fine / coarse" badge.
- **Arm 2** is two sub-arms over pressure-mode phases — a flow-arm (mean pressurized flow < 0.5 mL/s with sustained ≥ 4 bar for ≥ 15 s) and a yield-arm (`finalWeightG / targetWeightG < 0.70`). Either fires `chokedPuck=true`.

Both arms feed `DetectorResults.grindCoverage`, which already has three values defined in `openspec/specs/shot-analysis-pipeline/spec.md`:

- `"verified"` — at least one arm produced data.
- `"notAnalyzable"` — espresso, non-degenerate pour window, neither arm produced data.
- `"skipped"` — non-espresso beverage or `grind_check_skip` analysis flag.
- `""` (omitted) — pourTruncated cascade active or degenerate pour window.

The semantic problem this change addresses: Arm 1 is the only one of the four shot-analysis badges that requires profile context to be meaningful. Arm 1 reads the firmware-reported `flow_goal` series and compares actuals against it. On many profile shapes — lever-decline (flow goal is a safety limiter), dynamic-bloom (flow goal is a ramp-down command), or the first frame of any profile when the SFS bug pushes the pump-ramp transient into a "real" infuse frame — the flow goal is *not* a target the puck is supposed to track, and the comparison produces false positives.

The KB has a per-profile escape valve (`grind_check_skip` analysis flag, exercised by #1230 for `advanced-spring-lever`). But that requires per-profile authoring. For profiles we have *no* knowledge of, we currently still run Arm 1 — that's the population this change targets.

The matchProfileKey resolver path is already established (`src/ai/shotsummarizer_kb.cpp:376`): exact alias → #1198 longest-boundary-prefix → editor-type default → empty. Empty *is* the signal "we have no profile context". The kbId stored on every ShotRecord is either the resolved id or empty, so the signal is already available at every call site of `analyzeShot`.

## Goals / Non-Goals

**Goals:**

- Skip Arm 1 (and only Arm 1) when the profile's KB resolution returned empty.
- Surface the result through the existing `grindCoverage="notAnalyzable"` path so no new UI strings, no new badge logic, no new prose are introduced. The summary line "Could not analyze grind on this profile shape — …" already exists for this coverage value.
- Keep Arm 2 (yield-shortfall + sustained-pressurized-flow choke) running on unresolved profiles. Those arms read yield and sustained-pressure mean-flow — physics-level signals that don't depend on profile shape.
- Keep `pourTruncated`, `skipFirstFrame`, and channeling (with its existing skip flags) running unchanged on unresolved profiles. They catch missing baskets, gushers, the SFS firmware bug, and channeling regardless of profile shape.
- Preserve existing behaviour for KB-resolved profiles bit-for-bit. No corpus regressions on profiles the resolver knows about.

**Non-Goals:**

- Adding a new analysis flag. The absence of a KB resolution is itself the signal; `grind_check_skip` remains the way to opt a *known* profile out of Arm 1.
- Changing the projection logic in `src/history/shotbadgeprojection.h`. `grindIssueDetected` still requires `grindHasData` AND a fire condition — that contract is unchanged.
- Changing the verdict cascade. The existing "Clean shot, but grind could not be evaluated for this profile shape" verdict already covers `grindCoverage="notAnalyzable"`.
- Changing how live shots are analyzed during extraction. The live path (`ShotSummarizer::summarize(ShotDataModel*)`) runs through the same `analyzeShot` body — extending the signature flows through automatically.
- Distinguishing "user-created flow profile we don't know" from "renamed variant of a known KB profile". The #1198 prefix resolver already handles the second case; what's left is genuinely unknown profiles, and they all get the same `notAnalyzable` treatment.

## Decisions

### Decision 1: Pass the resolved-or-not bit as an explicit `analyzeShot` parameter, not a sentinel in `analysisFlags`

`analyzeShot` currently takes a `QStringList analysisFlags` populated from `ShotSummarizer::getAnalysisFlags(profileKbId)`. We could overload that list with a synthetic flag like `__unresolved__`, but that pattern would be a magic string in the public API and would mix two distinct concepts: authored-by-KB intent vs. resolver-derived state.

Instead, add `bool profileKbResolved` to `ShotAnalysis::analyzeShot` and `ShotAnalysis::analyzeFlowVsGoal` (default `true` for backward compatibility with direct test callers). Threading the bool stays close to the signal's source — every storage-layer call site already has `profileKbId` in scope and derives the bool with `!profileKbId.isEmpty()`.

**Alternatives considered:**
- Sentinel flag in `analysisFlags` — rejected as magic-string in a public list whose contract is "KB-authored opt-outs".
- New field on `AnalysisInputs` (storage-internal struct) — viable but adds a struct round-trip that the live-path entry doesn't share. An explicit parameter is the minimum cross-cut.

### Decision 2: Skip Arm 1's flow-mode-range building when `profileKbResolved=false`, do NOT set `skipped=true`

The `skipped=true` early-return path (line 333) is reserved for "skip the whole grind detector" (non-espresso beverage, `grind_check_skip` flag). It projects to `grindCoverage="skipped"`. That's not what we want here — we want Arm 2 to keep running, and we want the result to project as `"notAnalyzable"` (when Arm 2 also has no data) or `"verified"` (when Arm 2 fires).

Concretely, the implementation gates Arm 1's range-building block (lines 366-405 — the `if (!phases.isEmpty())` flow-mode-range builder) and the averaging block (lines 409-459) on `profileKbResolved`. When false, `flowModeRanges` stays empty, the averaging block is bypassed (it already early-exits when `flowModeRanges.isEmpty()`), and `GrindCheck.delta` / `sampleCount` keep their default zero values. Arm 2 then runs unconditionally as it does today.

`hasData` stays `false` unless Arm 2 fires (chokedPuck, yieldOvershoot, or yieldArm gates passing). `grindCoverage` then projects via the existing rules:

- Arm 2 produced data → `"verified"`.
- Arm 2 had no data → `"notAnalyzable"`.
- Pour truncated, beverage skip, or `grind_check_skip` → unchanged.

### Decision 3: Call-site derivation — read it from the same kbId every site already has

All `analyzeShot` call sites already have `profileKbId` in scope (the kbId stored on `ShotRecord` / `ShotSaveData` / `ShotSummary` is either resolved or empty). Add one local at each call site:

```cpp
const bool profileKbResolved = !profileKbId.isEmpty();
ShotAnalysis::analyzeShot(..., profileKbResolved);
```

Three storage call sites (saveShot at shothistorystorage.cpp:1122, loadShotRecordStatic at 1837, convertShotRecord at shothistorystorage_serialize.cpp:121) plus two ShotSummarizer entry points (summarize live at shotsummarizer.cpp:145, summarizeFromHistory at 457). That's it.

**Alternatives considered:**
- Put the bool on the `AnalysisInputs` struct — would consolidate the storage paths but doesn't help the ShotSummarizer call sites. Keep the parameter explicit for clarity.

### Decision 4: Default the parameter to `true` for direct test callers

`tst_shotanalysis.cpp` constructs synthetic shots and calls `analyzeFlowVsGoal` / `analyzeShot` directly without a KB context. Defaulting `profileKbResolved=true` preserves every existing test's behaviour without modification. New tests that explicitly exercise the `false` path pass it explicitly.

This is also the safe default for any future direct caller — a missing argument means "assume we know the profile and run all detectors", which matches the pre-change contract.

## Risks / Trade-offs

- **[Risk] Loss of legitimate Arm 1 signals on user-created flow-mode profiles we genuinely have no KB for** → Mitigation: Arm 2's yield-shortfall arm (`finalWeightG/targetWeightG < 0.70`) catches the canonical "grind too fine" failure shape (puck choked, yield missed target by 30 %+) regardless of profile context. The signal we're giving up is the more subtle "actual flow tracked goal but came in 0.5 mL/s under" diagnosis on unknown profiles, where we can't be confident the goal is a tracking target at all. The `notAnalyzable` coverage value already exists precisely so consumers can distinguish "verified clean" from "didn't analyze" — the AI advisor's prompt builder and the summary dialog already render that distinction honestly.

- **[Risk] Verdict text shift on existing user shots** → Mitigation: `grindCoverage="notAnalyzable"` already maps to the verdict "Clean shot, but grind could not be evaluated for this profile shape." instead of "Clean shot. Puck held well." (see `tests/tst_shotanalysis.cpp` scenarios for the existing notAnalyzable verdict). Users on unresolved profiles whose previous verdict was "clean and puck held well" will see the more-honest "clean but grind not evaluated" line. This is the *intent* of the existing notAnalyzable infrastructure; this change widens the population that hits it. No surprise to users — the verdict text already exists and was designed for this case.

- **[Risk] Regression-corpus shifts** → Mitigation: most corpus shots (`tests/data/shots/*.json`) carry KB-resolvable profile titles by design (`adaptive_v2_*`, `cremina_*`, `damian_lrv3_*`, `londinium_*`, etc.). Any that don't resolve become a deliberate audit point — surfaced by the shot_eval before/after diff. We accept verdict shifts on those that turn out to be intentional consequences of this change and update the corpus manifest accordingly.

- **[Trade-off] No per-profile granularity for "we know it well enough to run Arm 1"** — a KB entry that exists but doesn't actually contain grind-relevant guidance (e.g. a recipe with only `family` and `prose`, no `expertBand` or analysis flags) still flips `profileKbResolved=true`. That's fine: the absence of `grind_check_skip` is itself the author's "yes, please grade my profile" signal. Authors who decide Arm 1 isn't safe on a known family add the flag (the #1230 pattern).

- **[Trade-off] The "renamed user variants of unknown originals" case stays in the new bucket** — if a user picks an obscure shared community profile (no KB entry) and renames it, neither the exact alias lookup nor the #1198 prefix step resolves. That's the right answer for now; we genuinely have no signal that the renamed title is more analyzable than the unknown original.

## Migration Plan

No schema changes, no data migration. The detectors recompute on every shot load (per `docs/SHOT_REVIEW.md` §4), so:

- Existing shots on KB-resolved profiles: no change.
- Existing shots on KB-unresolved profiles where Arm 1 was producing a false-positive: re-render with `grindCoverage="notAnalyzable"`, the appropriate observation line, and the "clean but grind not evaluated" verdict (when no other badge fires). No user action required.
- Existing shots on KB-unresolved profiles where Arm 2 fires: unchanged (Arm 2 already produced the diagnosis).
- Live shots during extraction: same recompute pipeline; the live `ShotSummarizer::summarize` path takes the new parameter the same way.

Rollback: a one-line revert at each call site restores the previous behaviour. No state was committed; nothing was migrated.

## Open Questions

None at design time. Implementation may surface a fourth `analyzeShot` call site I missed; if so, treat it the same way — derive the bool from `profileKbId.isEmpty()` in scope.
