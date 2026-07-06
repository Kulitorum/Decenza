# shot-analysis-pipeline Delta Specification

## ADDED Requirements

### Requirement: Skip-first-frame guard suppresses only on confirmed exit reasons

`detectSkipFirstFrame` SHALL suppress the "First step skipped" detection only when the first non-zero frame's marker `transitionReason` is exactly a confirmed `pressure`, `flow`, or `weight` (case-insensitive). Unconfirmed reasons (`pressure_unconfirmed`, `flow_unconfirmed`), `time`, and empty values SHALL fall through to the duration-based checks, so a genuinely skipped or too-short first frame still flags.

#### Scenario: Confirmed sensor exit suppresses the badge

- **WHEN** the first non-zero frame's marker records `transitionReason = "pressure"` and the first frame ran shorter than the duration cutoff
- **THEN** `detectSkipFirstFrame` SHALL return false

#### Scenario: Unconfirmed sensor exit does not suppress the badge

- **WHEN** the first non-zero frame's marker records `transitionReason = "pressure_unconfirmed"` and the first frame ran shorter than the duration cutoff
- **THEN** `detectSkipFirstFrame` SHALL flag according to the duration-based checks (unchanged by the unconfirmed hint)

### Requirement: Grind limiter-tail trim fires on confirmed and unconfirmed pressure exits

`analyzeFlowVsGoal` SHALL trim the trailing limiter-tail window (`GRIND_LIMITER_TAIL_SKIP_SEC`) from a flow-mode phase when the next marker's `transitionReason` is `pressure` OR `pressure_unconfirmed` (case-insensitive), subject to the existing minimum post-trim window guard. Flow-flavored reasons (`flow`, `flow_unconfirmed`) SHALL NOT trigger the trim.

#### Scenario: Unconfirmed pressure exit trims the tail

- **WHEN** a flow-mode phase is followed by a marker with `transitionReason = "pressure_unconfirmed"` and the window is long enough to satisfy the post-trim minimum
- **THEN** the trailing `GRIND_LIMITER_TAIL_SKIP_SEC` SHALL be excluded from the flow-vs-goal averaging window

#### Scenario: Time exit leaves the window untrimmed

- **WHEN** a flow-mode phase is followed by a marker with `transitionReason = "time"`
- **THEN** the full window SHALL be used for flow-vs-goal averaging
