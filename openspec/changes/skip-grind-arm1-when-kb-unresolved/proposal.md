## Why

Grind Arm 1 (flow-vs-goal averaging in `ShotAnalysis::analyzeFlowVsGoal`) only produces a meaningful diagnosis when the profile's flow goal is a target the puck is supposed to track. On many real profiles the flow goal isn't that — it's a safety limiter on lever-decline frames, a firmware-generated ramp-down command on dynamic-bloom frames, or a pump-ramp curve on a first-frame that got swallowed by the BLE skip-first-step bug. Running Arm 1 on those profiles produces false-positive "grind too fine"/"too coarse" badges (#1205: a clean 32.4 g / 29.75 s shot on an Advanced Spring Lever variant landed at mean delta -0.399 mL/s, right on the 0.4 threshold, because the SFS bug pushed the pump-ramp transient into the `infuse` frame Arm 1 reads).

#1230 fixed the specific #1205 report by adding `grind_check_skip` to the `advanced-spring-lever` KB entry, but that path requires per-profile KB authoring. The broader pattern — "Arm 1 needs profile context to be meaningful; we have no profile context here" — affects every user-created profile that doesn't match a KB entry or editor-type default. The trigger for "I have no profile context" already exists in `ShotSummarizer::matchProfileKey`: it returns empty string when neither the exact alias lookup nor the #1198 longest-boundary-prefix step resolves, and no `dflow`/`aflow` editor-type hint is available. We can read that signal directly and degrade Arm 1 to the existing `grindCoverage="notAnalyzable"` path without inventing a new flag.

## What Changes

- `ShotAnalysis::analyzeFlowVsGoal` SHALL accept a new caller-supplied boolean `profileKbResolved` (default `true`). When `false`, the function SHALL skip Arm 1's flow-mode-range construction and flow-vs-goal averaging entirely — `sampleCount` and `delta` stay at their default-zero values, `hasData` stays `false` for Arm 1. Arm 2 (choked-puck + yield-shortfall) runs unchanged.
- `ShotAnalysis::analyzeShot` SHALL determine `profileKbResolved` from its caller and propagate it to `analyzeFlowVsGoal`. The signal already flows into the analyze path indirectly via `analysisFlags` (today populated from `ShotSummarizer::getAnalysisFlags(profileKbId)`); we extend the same call site to also pass the resolved-or-not bit.
- The existing `grindCoverage="notAnalyzable"` projection SHALL continue to fire when Arm 1's `hasData=false` AND Arm 2's gate doesn't pass. This change widens the *population* that hits that projection — profiles with no KB resolution now fall into it instead of running Arm 1 on a meaningless flow-goal series — without changing the projection logic itself.
- The existing `[observation]` summary line "Could not analyze grind on this profile shape — …" SHALL continue to fire on the `notAnalyzable` coverage. No new prose, no new badge.
- No new analysis flag. The absence of a KB resolution is itself the signal — adding `grind_check_skip` as an authored flag remains the way to opt a *known* profile out of Arm 1 (the #1230 path).
- Detectors that don't depend on profile shape — `pourTruncated` (peak pressure < 2.5 bar), `skipFirstFrame`, channeling (already gated by `channeling_expected` flag and `shouldSkipChannelingCheck` heuristics), and grind Arm 2 (yield-shortfall and sustained-pressurized-flow choke) — SHALL keep running unchanged on unresolved profiles.

## Capabilities

### New Capabilities

(none — extends the existing shot-analysis-pipeline contract)

### Modified Capabilities

- `shot-analysis-pipeline`: the `grindCoverage="notAnalyzable"` requirement adds a second qualifying precondition — Arm 1 SHALL also be skipped (its `hasData` SHALL remain `false`) when the profile's KB resolution is empty AND no editor-type hint is available, projecting into the same `notAnalyzable` coverage value the existing path already produces.

## Impact

- Code: `src/ai/shotanalysis.h` (add `profileKbResolved` parameter to `analyzeFlowVsGoal` and `analyzeShot`); `src/ai/shotanalysis.cpp` (gate Arm 1's flow-mode-range build on the new flag; thread the parameter through `analyzeShot`); `src/history/shothistorystorage.cpp` (and any other call site of `analyzeShot`) to pass the resolved-or-not bit — derive it from the existing `ShotSummarizer::matchProfileKey(profileTitle, profileType).isEmpty()` call already adjacent to the `getAnalysisFlags` lookup.
- Tests: extend `tests/tst_shotanalysis.cpp` with three scenarios — (1) `profileKbResolved=false` on a flow-mode shot whose Arm 1 would otherwise fire skips Arm 1 cleanly and projects `grindCoverage="notAnalyzable"`; (2) same shot with `profileKbResolved=true` runs Arm 1 unchanged (no behavioural change for resolved profiles); (3) `profileKbResolved=false` on a yield-shortfall shot still fires the grind badge via Arm 2.
- Regression corpus (`tests/data/shots/`): inventory which corpus shots are on KB-unresolved profiles and confirm verdict shifts are intentional. Most corpus shots are on KB-known profiles and won't move. Any shift surfaces during the shot_eval before/after diff.
- Docs: update `docs/SHOT_REVIEW.md` §2.2 "Grind issue" to document the new precondition for the `notAnalyzable` coverage.
- No BLE, no DB schema, no QML, no settings changes. No user-visible UI change beyond the summary line that already exists.
