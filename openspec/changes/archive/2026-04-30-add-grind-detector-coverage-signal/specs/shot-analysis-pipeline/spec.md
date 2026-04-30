# shot-analysis-pipeline delta

## ADDED Requirements

### Requirement: Grind detector SHALL emit a coverage signal distinguishing verified-clean from not-analyzable

The grind detector SHALL emit a `grindCoverage` signal taking one of three values (`"verified"`, `"notAnalyzable"`, `"skipped"`) so that the system can distinguish a positively-verified clean grind from the absence of analyzable data. When `ShotAnalysis::analyzeFlowVsGoal` runs against an espresso shot whose beverage type and analysis flags do not gate it out (`skipped == false`), the function SHALL set `GrindCheck::hasData = true` whenever its choked-puck loop sees BOTH `flowSamples >= 5` AND `pressurizedDuration >= 15.0 s` (the existing gate constants `CHOKED_PRESSURE_MIN_BAR = 4.0` and `CHOKED_DURATION_MIN_SEC = 15.0`), regardless of whether `flowChoked` or `yieldShortfall` fires. When neither sub-arm fires, the function SHALL ALSO set a new `GrindCheck::verifiedClean = true` flag.

`ShotAnalysis::analyzeShot` SHALL populate a new `DetectorResults::grindCoverage`
field with one of three string values:

- `"verified"` — `GrindCheck.hasData == true`. The detector ran with enough
  data to produce a result. Set whether or not the result is healthy: a
  shot whose Arm 2 gates passed without a choke firing AND a shot that
  fired `chokedPuck`, `yieldOvershoot`, or a large `delta` BOTH carry
  `coverage = "verified"`. The verdict and `grindDirection` already
  carry the specific diagnosis; coverage signals data availability,
  not health outcome. Consumers wanting "verified clean" specifically
  should read `grindVerifiedClean` directly.
- `"notAnalyzable"` — `GrindCheck.hasData == false && GrindCheck.skipped == false`,
  AND the espresso shot's pour window was non-degenerate (`pourEndSec > pourStartSec`),
  AND the beverage type is not in the non-espresso skip list.
- `"skipped"` — `GrindCheck.skipped == true` (non-espresso beverages or profiles
  carrying the `grind_check_skip` analysis flag).

When the pourTruncated cascade is active, the field SHALL be omitted entirely
(consistent with how the channeling, flow-trend, temp, and grind blocks are
already suppressed in that cascade).

The five quality-badge boolean projections in `src/history/shotbadgeprojection.h`
SHALL NOT change. Specifically: `grindIssueDetected` SHALL still require
`grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`.
A verified-clean result SHALL project `grindIssueDetected = false`.

#### Scenario: Verified-clean shot emits a positive signal

- **GIVEN** an espresso shot with beverage type `"espresso"` and a healthy
  pressurized pour (≥ 5 flow samples, ≥ 15 s sustained at ≥ 4 bar)
- **AND** mean pressurized flow ≥ 0.5 mL/s
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.85`
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `summaryLines` SHALL contain one entry with `type = "good"` and text
  "Grind tracked goal during pour"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Profile shape that defeats both arms emits an honest signal

- **GIVEN** an espresso shot whose phase markers are exclusively flow-mode OR
  whose pressurized duration is below 15 s
- **AND** the choked-puck arm produces no usable data (`flowSamples < 5` OR
  `pressurizedDuration < 15.0 s`)
- **AND** the flow-vs-goal arm produces no usable data (no flow-mode samples
  in the pour window with `goal >= 0.3 mL/s`)
- **AND** the beverage type is `"espresso"`
- **AND** the pour window is non-degenerate (`pourEndSec > pourStartSec`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one entry with `type = "observation"`
  and text starting with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Choked puck verdict is unchanged

- **GIVEN** an espresso shot whose mean pressurized flow is below 0.5 mL/s
  AND the choked-puck arm gates pass
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"` (the gates
  passed, so the loop has data) — but `chokedPuck` SHALL be `true` and the
  existing "Puck choked" warning line and verdict SHALL fire identically to
  prior behavior
- **AND** `verifiedClean` SHALL be `false`
- **AND** `grindIssueDetected` SHALL be `true`

#### Scenario: Pour-truncated cascade suppresses the coverage signal

- **GIVEN** an espresso shot where `pourTruncated == true` (peak pressure
  inside the pour window is below `PRESSURE_FLOOR_BAR`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL be absent from the structured
  output
- **AND** `summaryLines` SHALL NOT contain the new "Grind tracked goal" line
  NOR the new "Could not analyze grind" line
- **AND** the existing pourTruncated cascade behavior SHALL apply unchanged

### Requirement: Verdict cascade SHALL acknowledge a not-analyzable grind result

The verdict cascade SHALL distinguish "clean and verified" from "clean but grind not analyzable" so users on lever / two-frame profiles see honest UI text. When the verdict cascade in `analyzeShot` reaches the "Otherwise" terminal branch (no warnings, no cautions) AND `DetectorResults.grindCoverage == "notAnalyzable"`, the cascade SHALL emit the verdict text "Clean shot, but grind could not be evaluated for this profile shape." instead of the existing "Clean shot. Puck held well."

When the cascade reaches the same terminal branch with `grindCoverage ==
"verified"` or `grindCoverage` absent (e.g. non-espresso skipped path), the
cascade SHALL emit the existing "Verdict: Clean shot. Puck held well." text
unchanged.

The `verdictCategory` enum string emitted on `DetectorResults` SHALL gain a
new value `"cleanGrindNotAnalyzable"` for the new branch. The existing
`"clean"` value SHALL continue to apply when grind was verified or skipped.

#### Scenario: Verified-clean shot keeps the existing clean verdict

- **GIVEN** an espresso shot with `grindCoverage == "verified"` AND no
  warning/caution lines
- **WHEN** the verdict cascade runs
- **THEN** the verdict line SHALL read "Verdict: Clean shot. Puck held well."
- **AND** `verdictCategory` SHALL equal `"clean"`

#### Scenario: Not-analyzable shot gets the honest verdict

- **GIVEN** an espresso shot with `grindCoverage == "notAnalyzable"` AND no
  warning/caution lines
- **WHEN** the verdict cascade runs
- **THEN** the verdict line SHALL read "Verdict: Clean shot, but grind could
  not be evaluated for this profile shape."
- **AND** `verdictCategory` SHALL equal `"cleanGrindNotAnalyzable"`

#### Scenario: Warnings or cautions take precedence over the new verdict

- **GIVEN** an espresso shot with `grindCoverage == "notAnalyzable"` AND
  any other detector emits a warning or caution line
- **WHEN** the verdict cascade runs
- **THEN** the verdict SHALL come from the existing precedence rules
  (pourTruncated → skipFirstFrame → chokedPuck → "Puck integrity issue" →
  caution-grind direction → "Decent shot with minor issues to watch"),
  NOT from the new "grind could not be evaluated" branch
