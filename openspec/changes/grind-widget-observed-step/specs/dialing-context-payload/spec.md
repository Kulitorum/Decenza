## ADDED Requirements

### Requirement: grinderContext SHALL report a noise-filtered typical step

`grinderContext` SHALL carry a `stepSize` field giving the typical dial increment between the user's observed numeric settings for the grinder, derived by a robust estimator that rejects lone outliers. The estimator SHALL be a shared helper used by both the `dialing_get_context` MCP payload and the equivalent in-app AI user-prompt enrichment, so the two surfaces report the same value.

The estimator SHALL operate on the sorted, de-duplicated numeric settings and choose the most frequently occurring gap between consecutive values (rounding gaps to absorb floating-point noise), breaking ties toward the smaller gap, and clamping the result to a sane floor so a stray fine-typo cluster cannot produce a useless micro-step. `stepSize` SHALL be present only when at least two distinct numeric settings are available; otherwise it SHALL be omitted.

This replaces the former `smallestStep` field, which reported the raw minimum gap and was therefore collapsed to a spurious value by a single mistyped setting.

#### Scenario: Typical step from clean history

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be `0.25`
- **AND** the payload SHALL NOT carry a `smallestStep` field

#### Scenario: A lone outlier does not collapse the step

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9 plus a single `8.1`
- **WHEN** `grinderContext.stepSize` is derived
- **THEN** it SHALL remain `0.25`, not `0.1`

#### Scenario: Insufficient data omits the step

- **GIVEN** the grinder has fewer than two distinct numeric settings in history
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be omitted

### Requirement: Dial-in blocks SHALL pair RPM with the grind setting

For variable-RPM grinders the dial-in has two components — the burr grind setting and the motor RPM — so every dial-in surface that carries `grinderSetting` SHALL also carry the sibling `rpm` when one is recorded (`rpm > 0`), emitted sparsely so legacy/non-RPM shots are unchanged. RPM is a shot-variable field: in `dialInSessions` it SHALL appear on the per-shot entry, never the hoisted session `context`.

This applies to: `dialInSessions[].shots[]` entries, `bestRecentShot`, the `changeFromPrev` / `changeFromBest` diffs (which SHALL report an RPM change when it differs), and the prose shot-summary grind line (which SHALL render the RPM alongside the setting). `grinderContext` SHALL additionally summarize the user's observed RPMs (observed values, range, and a noise-filtered RPM step derived by the same estimator) when the grinder is RPM-capable.

#### Scenario: Per-shot RPM in dialInSessions

- **GIVEN** a session whose shots were pulled at grind `2.4` and RPM `1400`
- **WHEN** `dialInSessions` is built
- **THEN** each shot entry SHALL carry `grinderSetting: "2.4"` AND `rpm: 1400`
- **AND** `rpm` SHALL NOT appear in the session `context` hoist (it is shot-variable)

#### Scenario: RPM change appears in the diff

- **GIVEN** two consecutive shots identical except RPM moved 1400 → 1350
- **WHEN** the `changeFromPrev` diff is built
- **THEN** it SHALL report the RPM change

#### Scenario: Sparse — no RPM noise on non-RPM shots

- **GIVEN** a shot with no recorded RPM (`rpm` is 0)
- **WHEN** any dial-in block carrying its grind setting is built
- **THEN** no `rpm` field SHALL be emitted for that shot

### Requirement: The advisor SHALL be able to recommend and score an RPM change

The `nextShot` structured-output schema SHALL include an optional `rpm` (integer) field the advisor emits only when it recommends a motor-RPM change, and only for variable-RPM grinders — independent of `grinderSetting` (either, both, or neither may be recommended). Adherence tracking SHALL score the `rpm` recommendation with the same "matched within tolerance AND the user actually moved" discipline used for grind, and the recommendation/outcome renderers SHALL surface the RPM alongside the grind.

#### Scenario: RPM recommendation is tracked

- **GIVEN** an advisor turn whose `nextShot` recommends `rpm: 1350` (no grind change)
- **AND** the user's next shot is pulled at 1350 RPM (moved from a prior 1400)
- **WHEN** the `recentAdvice` block is built
- **THEN** the recommendation SHALL list the predicted RPM
- **AND** adherence SHALL count the RPM as followed

#### Scenario: RPM only offered for variable-RPM grinders

- **GIVEN** a fixed-RPM grinder
- **THEN** the schema guidance SHALL instruct the model to omit `rpm`
