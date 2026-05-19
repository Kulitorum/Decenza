## ADDED Requirements

### Requirement: Grind Arm 1 (flow-vs-goal averaging) SHALL be skipped on KB-unresolved profiles

`ShotAnalysis::analyzeShot` and `ShotAnalysis::analyzeFlowVsGoal` SHALL accept a `bool profileKbResolved` parameter (defaulting to `true` for backward compatibility with direct test callers and other in-process invocations). The parameter SHALL be `true` when the profile's KB resolution via `ShotSummarizer::matchProfileKey(profileTitle, profileType)` produced a non-empty id (exact alias hit, #1198 longest-boundary-prefix hit, or editor-type-default hit), and `false` otherwise.

When `profileKbResolved == false`, `analyzeFlowVsGoal` SHALL skip Arm 1 (the flow-vs-goal averaging block over flow-mode phases) entirely. Specifically: the flow-mode-range builder SHALL NOT populate `flowModeRanges`, the sample-averaging loop SHALL NOT run, and `GrindCheck::delta`, `GrindCheck::sampleCount`, and the Arm 1 contribution to `GrindCheck::hasData` SHALL remain at their default-zero / `false` values. The function SHALL NOT set `GrindCheck::skipped = true` — that field remains reserved for the existing non-espresso-beverage and `grind_check_skip` analysis-flag paths whose semantics differ (full grind detector suppression, distinct `grindCoverage = "skipped"` projection).

When `profileKbResolved == false`, Arm 2 (the choked-puck flow-arm and yield-shortfall yield-arm over pressure-mode phases) SHALL continue to run unchanged. The two arms read sustained-pressurized mean flow and `finalWeightG / targetWeightG` — physics-level signals that do not depend on profile shape and remain meaningful on profiles with no KB context.

When `profileKbResolved == true`, both arms SHALL run exactly as they do today. This change SHALL NOT alter behaviour for any KB-resolvable profile (exact alias, #1198 prefix match, or editor-type default).

The existing `grindCoverage` projection rules SHALL apply unchanged. With Arm 1 skipped and Arm 2's outcome driving `GrindCheck::hasData`:

- Arm 2 produced data (its `flowSamples >= 5` AND (`pressurizedDuration >= 15 s` OR yield-shortfall arm fired)) → `grindCoverage = "verified"`.
- Arm 2 had no data → `grindCoverage = "notAnalyzable"` (for an espresso shot with a non-degenerate pour window).
- Pour-truncated cascade active → `grindCoverage` omitted, as today.

The existing `[observation]` summary line "Could not analyze grind on this profile shape — …" SHALL fire on the resulting `"notAnalyzable"` coverage value via the existing emission path. No new prose, no new badge, no new verdict text is introduced by this change — the existing notAnalyzable infrastructure already covers the user-visible surface. The existing `verdictCategory = "cleanGrindNotAnalyzable"` SHALL apply when the cascade reaches the terminal "Otherwise" branch with this coverage.

The four quality-badge boolean projections in `src/history/shotbadgeprojection.h` SHALL NOT change. `grindIssueDetected` still requires `grindHasData && (grindChokedPuck || grindYieldOvershoot || |grindFlowDeltaMlPerSec| > FLOW_DEVIATION_THRESHOLD)`. With Arm 1 skipped and Arm 2 silent, `grindHasData` is `false` and `grindIssueDetected` projects to `false` — the false-positive surface this change targets.

Detectors other than grind SHALL be unaffected by `profileKbResolved`. `pourTruncated` (peak-pressure floor), `skipFirstFrame` (phase-marker-based), and channeling (with its existing `channeling_expected` flag and `shouldSkipChannelingCheck` heuristics) SHALL run identically on KB-resolved and KB-unresolved profiles.

#### Scenario: Unresolved profile with a clean Arm 2 result projects as verified-clean

- **GIVEN** an espresso shot on a profile whose title and editor type both fail to resolve via `ShotSummarizer::matchProfileKey` (`profileKbResolved == false`)
- **AND** the shot's pressure-mode phases produce a healthy pressurized pour (≥ 5 flow samples at ≥ 4 bar, ≥ 15 s pressurized duration, mean pressurized flow ≥ 0.5 mL/s)
- **AND** `finalWeightG / targetWeightG >= 0.70`
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped (`GrindCheck.sampleCount == 0` AND `GrindCheck.delta == 0`)
- **AND** Arm 2 SHALL run and set `GrindCheck.hasData = true` AND `GrindCheck.verifiedClean = true`
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `grindIssueDetected` SHALL be `false`
- **AND** `summaryLines` SHALL contain the existing "Grind tracked goal during pour" `[good]` line

#### Scenario: Unresolved profile with no Arm 2 data projects as not-analyzable

- **GIVEN** an espresso shot on a KB-unresolved profile whose pour window is non-degenerate
- **AND** the pour produces fewer than 5 samples at ≥ 4 bar (Arm 2 flow-arm gate fails on `flowSamples`)
- **AND** either `targetWeightG == 0` OR `finalWeightG / targetWeightG >= 0.70` (Arm 2 yield-arm also silent)
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped (no flow-vs-goal averaging)
- **AND** Arm 2 SHALL run but produce `hasData == false`
- **AND** `DetectorResults.grindCoverage` SHALL equal `"notAnalyzable"`
- **AND** `summaryLines` SHALL contain one `[observation]` entry whose text starts with "Could not analyze grind on this profile shape"
- **AND** `grindIssueDetected` SHALL be `false`
- **AND** the verdict cascade's terminal branch SHALL emit "Clean shot, but grind could not be evaluated for this profile shape." with `verdictCategory = "cleanGrindNotAnalyzable"` when no other detector fires

#### Scenario: Unresolved profile with a yield shortfall still fires the grind badge via Arm 2

- **GIVEN** an espresso shot on a KB-unresolved profile
- **AND** `flowSamples >= 5` AND `targetWeightG > 0` AND `finalWeightG > 0`
- **AND** `(finalWeightG / targetWeightG) < 0.70` (e.g. 23 g of a 36 g target)
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** Arm 1 SHALL be skipped
- **AND** Arm 2's yield-shortfall arm SHALL fire (`chokedPuck = true`, `hasData = true`)
- **AND** `DetectorResults.grindCoverage` SHALL equal `"verified"`
- **AND** `grindIssueDetected` SHALL be `true`
- **AND** the existing "Pour produced near-zero flow while pressure held — puck choked" warning SHALL be emitted unchanged

#### Scenario: Resolved profile runs Arm 1 exactly as before

- **GIVEN** an espresso shot on a profile whose title or editor type DOES resolve via `ShotSummarizer::matchProfileKey` (exact alias, #1198 prefix, or editor-type default)
- **AND** the shot data is otherwise unchanged from a pre-change run
- **WHEN** `analyzeShot` runs with `profileKbResolved = true`
- **THEN** Arm 1's flow-mode-range builder, stationarity gate, and averaging loop SHALL execute identically to the pre-change behaviour
- **AND** `GrindCheck.delta`, `GrindCheck.sampleCount`, `GrindCheck.hasData`, and `DetectorResults.grindCoverage` SHALL all carry the same values they would have on the pre-change build

#### Scenario: Profile-agnostic detectors run on unresolved profiles

- **GIVEN** an espresso shot on a KB-unresolved profile where the puck fails to build pressure (peak pressure < 2.5 bar) OR phase markers show frame 0 was never observed before a non-zero frame at phase.time < 2.0 s
- **WHEN** `analyzeShot` runs with `profileKbResolved = false`
- **THEN** `pourTruncated` and/or `skipFirstFrame` SHALL fire identically to the resolved-profile case
- **AND** the existing badge cascades (pourTruncated dominating channeling/grind; skipFirstFrame independent) SHALL apply unchanged

#### Scenario: Direct test callers default profileKbResolved to true

- **GIVEN** a unit test that calls `ShotAnalysis::analyzeShot(...)` or `ShotAnalysis::analyzeFlowVsGoal(...)` without supplying the new `profileKbResolved` parameter
- **WHEN** the call executes
- **THEN** the default value `profileKbResolved = true` SHALL apply
- **AND** Arm 1 SHALL run with its existing behaviour, preserving every pre-change test scenario without modification
