# shot-analysis-pipeline delta

## MODIFIED Requirements

### Requirement: Grind detector SHALL emit a coverage signal distinguishing verified-clean from not-analyzable

The grind detector SHALL emit a `grindCoverage` signal taking one of three values (`"verified"`, `"notAnalyzable"`, `"skipped"`) so that the system can distinguish a positively-verified clean grind from the absence of analyzable data. When `ShotAnalysis::analyzeFlowVsGoal` runs against an espresso shot whose beverage type and analysis flags do not gate it out (`skipped == false`), the function SHALL evaluate the choked-puck arms with split gating: the **flow-choked arm** SHALL fire only when ALL of `flowSamples >= 5` AND `pressurizedDuration >= CHOKED_DURATION_MIN_SEC` (15.0 s) AND mean pressurized flow `< CHOKED_FLOW_MAX_MLPS` (0.5 mL/s) hold. The **yield-shortfall arm** SHALL fire when ALL of `flowSamples >= 5` AND `targetWeightG > 0.0` AND `finalWeightG > 0.0` AND `(finalWeightG / targetWeightG) < CHOKED_YIELD_RATIO_MAX` (0.70 — tightened from a prior 0.85 by audit) hold. The yield arm SHALL NOT require `pressurizedDuration >= 15.0 s` — its diagnosis is yield-based and does not read pressurized flow.

The function SHALL set `GrindCheck::hasData = true` whenever EITHER arm could speak: when `flowSamples >= 5` AND (`pressurizedDuration >= 15.0 s` OR the yield-shortfall arm fired). The function SHALL set `GrindCheck::verifiedClean = true` only when `flowSamples >= 5` AND `pressurizedDuration >= 15.0 s` AND neither sub-arm fired AND `|delta| <= FLOW_DEVIATION_THRESHOLD` AND `yieldOvershoot == false`. The verified-clean signal still requires the strong flow-arm gates because it asserts a healthy sustained pressurized pour.

`ShotAnalysis::analyzeShot` SHALL populate a `DetectorResults::grindCoverage` field with one of three string values:

- `"verified"` — `GrindCheck.hasData == true`. The detector ran with enough data to produce a result. Set whether or not the result is healthy: a verified-clean pour AND a chokedPuck/yieldOvershoot/large-delta pour BOTH carry `coverage = "verified"`. Coverage signals data availability, not health outcome — read `grindVerifiedClean` / `grindDirection` / verdict for the diagnosis.
- `"notAnalyzable"` — `GrindCheck.hasData == false && GrindCheck.skipped == false`, AND the espresso shot's pour window was non-degenerate (`pourEndSec > pourStartSec`), AND the beverage type is not in the non-espresso skip list.
- `"skipped"` — `GrindCheck.skipped == true` (non-espresso beverages or profiles carrying the `grind_check_skip` analysis flag).

When the pourTruncated cascade is active, the field SHALL be omitted entirely (consistent with how the channeling, flow-trend, temp, and grind blocks are already suppressed in that cascade).

The five quality-badge boolean projections in `src/history/shotbadgeprojection.h` SHALL NOT change. Specifically: `grindIssueDetected` SHALL still require `grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`. A verified-clean result SHALL project `grindIssueDetected = false`. A yield-shortfall-only result (yield arm fired, flow arm gates didn't pass) SHALL project `grindIssueDetected = true` because `chokedPuck` is set when either choke sub-arm fires.

#### Scenario: Verified-clean shot emits a positive signal

- **GIVEN** an espresso shot with beverage type `"espresso"` and a healthy pressurized pour (≥ 5 flow samples, ≥ 15 s sustained at ≥ 4 bar)
- **AND** mean pressurized flow ≥ 0.5 mL/s
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.70`
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `summaryLines` SHALL contain one entry with `type = "good"` and text "Grind tracked goal during pour"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Profile shape that defeats both arms emits an honest signal

- **GIVEN** an espresso shot whose phase markers are exclusively flow-mode OR whose pressurized duration is below 15 s
- **AND** the choked-puck arm produces no usable data (`flowSamples < 5` — i.e. no pressurized samples at all)
- **AND** the flow-vs-goal arm produces no usable data (no flow-mode samples in the pour window with `goal >= 0.3 mL/s`)
- **AND** the beverage type is `"espresso"`
- **AND** the pour window is non-degenerate (`pourEndSec > pourStartSec`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one entry with `type = "observation"` and text starting with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Choked puck verdict is unchanged on the flow arm

- **GIVEN** an espresso shot whose mean pressurized flow is below 0.5 mL/s AND the flow-choked arm gates pass (≥ 5 samples AND ≥ 15s pressurized)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL equal `"verified"` (the flow-arm gates passed)
- **AND** `chokedPuck` SHALL be `true` and the existing "Puck choked" warning line and verdict SHALL fire identically to prior behavior
- **AND** `verifiedClean` SHALL be `false`
- **AND** `grindIssueDetected` SHALL be `true`

#### Scenario: Yield-shortfall arm fires on a brief-pressurized shot

- **GIVEN** an espresso shot with `flowSamples >= 5` (puck saw meaningful pressure briefly) AND `pressurizedDuration < 15 s` (flow-arm gate did NOT pass)
- **AND** `targetWeightG > 0` AND `finalWeightG > 0`
- **AND** `(finalWeightG / targetWeightG) < 0.70` (e.g. 23.1g of a 36g target = 0.64)
- **WHEN** `analyzeShot` runs
- **THEN** `chokedPuck` SHALL be `true` (yield arm fired)
- **AND** `hasData` SHALL be `true`
- **AND** `verifiedClean` SHALL be `false` (flow-arm gates required for verification)
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"` (an arm produced data)
- **AND** `grindIssueDetected` SHALL be `true`
- **AND** `summaryLines` SHALL include the existing "Pour produced near-zero flow while pressure held — puck choked" warning

#### Scenario: Borderline yield ratio between 0.70 and 0.85 stays silent

- **GIVEN** an espresso shot with `flowSamples >= 5` AND `pressurizedDuration < 15 s`
- **AND** `(finalWeightG / targetWeightG) = 0.75` (above 0.70 threshold but below the prior 0.85)
- **WHEN** `analyzeShot` runs
- **THEN** `chokedPuck` SHALL be `false`
- **AND** the audit-driven 0.70 threshold SHALL hold; no warning line about "Pour produced near-zero flow" SHALL fire
- **AND** `grindIssueDetected` SHALL be `false`

#### Scenario: Pour-truncated cascade suppresses the coverage signal

- **GIVEN** an espresso shot where `pourTruncated == true` (peak pressure inside the pour window is below `PRESSURE_FLOOR_BAR`)
- **WHEN** `analyzeShot` runs
- **THEN** `DetectorResults.grindCoverage` SHALL be absent from the structured output
- **AND** `summaryLines` SHALL NOT contain the new "Grind tracked goal" line NOR the new "Could not analyze grind" line
- **AND** the existing pourTruncated cascade behavior SHALL apply unchanged
