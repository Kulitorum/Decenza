# Change: Add grind-detector coverage signal — verified-clean vs not-analyzable

## Why

A 500-shot audit against the live shot history found that **~50% of espresso
shots silently produce `det.grind.hasData = false`** while still showing the
verdict "Clean shot. Puck held well." Affected profiles concentrate on
two-marker (Preinfusion + Pour) shapes:

| Profile                          | hasData=false rate |
|----------------------------------|--------------------|
| A-Flow / default-medium          | 100% (52/52)       |
| D-Flow / La Pavoni / La Pavoni 80s | 100% (25/25)     |
| D-Flow / Malabar                 | 99%  (107/108)     |
| D-Flow / Luca's Italian Style    | 87%  (7/8)         |
| D-Flow / Q                       | 30%  (55/182)      |

Root cause (verified by reading `analyzeFlowVsGoal` against representative
shots Malabar #459, La Pavoni #277, A-Flow #604):

1. **Arm 1 (flow-vs-goal)** builds flow-mode time ranges from phase markers,
   then skips every flow sample where `fp.x() < pourStart`. On a two-marker
   profile (Preinfusion → Pour), the only flow-mode range is
   `[0, pourStart]` — i.e. *entirely before* `pourStart`. Zero qualifying
   samples, arm goes silent.

2. **Arm 2 (choked-puck)** sets `result.hasData = true` only inside its
   `if (flowChoked || yieldShortfall)` branch. When pressure is healthy and
   yield is reasonable, the arm correctly says "no choke" but never sets
   hasData. There is no positive "all clear" signal.

3. **Yield-overshoot arm** requires `targetWeightG > 0`. Many of these
   profiles save `targetWeightG = 0` (user uses SAW or eyeballs), silencing
   the arm.

The downstream effect is that the verdict text claims "Puck held well." on
every shot from these profiles regardless of whether the puck actually held
or the system simply could not analyze it. That's *integrity* harm — users
learn the badge system is unreliable on lever / two-frame profiles, then
either ignore badges or distrust them when a real detector eventually fires.

## What Changes

- **Arm 2 emits a positive "verified clean" signal.** When the choked-puck
  loop sees `flowSamples >= 5 && pressurizedDuration >= 15s` AND neither
  `flowChoked` nor `yieldShortfall` fires, set `result.hasData = true` AND
  set a new `result.verifiedClean = true` flag (without setting
  `chokedPuck`). The user-facing summary gains a `[good]` line acknowledging
  the verification.

- **A "could not analyze grind" signal replaces the misleading silent
  pass.** When `analyzeFlowVsGoal` returns `hasData=false AND skipped=false`
  on an espresso shot whose pour window was non-degenerate, expose the
  result as a new `det.grind.coverage = "notAnalyzable"` value. The summary
  emits an `[observation]` line "Could not analyze grind on this profile
  shape." The verdict cascade replaces the bare "Clean shot. Puck held
  well." with "Clean shot, but grind could not be evaluated for this
  profile" when the only analytic absence is the grind detector.

- **No badge changes.** The five quality-badge booleans (`grindIssueDetected`,
  etc.) are untouched. `hasData=true && !chokedPuck && !yieldOvershoot &&
  |delta| <= threshold` still does NOT set `grindIssueDetected`. The
  proposed change adds positive UI signal in the summary dialog and
  detector-results MCP payload without altering the cascade or projection.

- **Simulation outcome on the 253 shot population that currently silences
  the grind detector** (faithful port of Arm 2 against cached curves):

  | Outcome | Count | What the user sees |
  |---|---|---|
  | Newly emits "verified clean" (Option 1) | **134 (53%)** | New `[good]` summary line backed by data |
  | Newly emits "couldn't analyze" (Option 3) | **115 (45%)** | Honest `[observation]` line + adjusted verdict |
  | Newly detected as choked (would be a false positive) | **0** | Safe — Arm 2's choke condition unchanged |
  | Inverted-window (still silent) | 4 | Orthogonal — separate fix |

  Total useful coverage: **249/253 (98%)** of formerly-silent shots gain
  meaningful UI signal.

## Impact

- Affected specs: `shot-analysis-pipeline` (this change adds the coverage
  signal contract).
- Affected code:
  - `src/ai/shotanalysis.{h,cpp}` — new `GrindCheck::verifiedClean` field;
    Arm 2 sets `hasData=true` once gates pass; new
    `DetectorResults.grindCoverage` enum-string field; new summary line in
    `analyzeShot`; verdict-cascade prose adjustment.
  - `src/history/shothistorystorage_serialize.cpp` — emit
    `det.grind.coverage` in `convertShotRecord`'s structured output.
  - `tests/tst_shotanalysis.cpp` — regression coverage for the four
    transitions (verified-clean, not-analyzable, still-choked,
    pourTruncated-suppressed).
  - `tools/shot_eval/main.cpp` — already exercises `analyzeShot`'s line list
    via `generateSummary`; manifest may need an updated golden line for
    affected fixtures.
- No QML changes required: `ShotAnalysisDialog.qml` already renders all
  `[good]` / `[observation]` line types from `summaryLines`.
- No DB migration: badge columns unchanged; summary lines are recomputed
  on every load.
- `docs/SHOT_REVIEW.md` — update §2.2 (grind detector internals) to
  document the new positive-signal branch and the `grindCoverage` value.
