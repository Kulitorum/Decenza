# frame-transition-reason Specification

## ADDED Requirements

### Requirement: Transition reason vocabulary distinguishes confirmed from unconfirmed exits

Each phase marker's `transitionReason` SHALL record why the preceding frame exited, using exactly one of: `weight`, `pressure`, `flow` (confirmed exits), `pressure_unconfirmed`, `flow_unconfirmed` (likely-but-unconfirmed sensor exits), `time` (time-based exit), or empty (unknown / pre-feature data). A confirmed sensor value (`pressure`, `flow`) SHALL be recorded only when the corresponding sensor reading satisfied the frame's configured exit threshold at the transition sample; `weight` SHALL be recorded only for an app-initiated weight skip. Consumers MAY treat confirmed values as ground truth.

#### Scenario: Confirmed pressure exit

- **WHEN** a frame with a `pressure_over` exit condition transitions and the latest sensor sample is at or above the configured threshold
- **THEN** the new frame's marker SHALL record `transitionReason = "pressure"`

#### Scenario: Ambiguous exit records an unconfirmed sensor hint

- **WHEN** a frame with a configured exit condition transitions, the latest sensor sample does not satisfy the threshold, and less than 90% of the frame's configured duration has elapsed
- **THEN** the marker SHALL record `pressure_unconfirmed` or `flow_unconfirmed` according to the frame's exit type, and SHALL NOT record a confirmed `pressure`/`flow`/`weight` value

#### Scenario: Time expiry still records time

- **WHEN** a frame transitions after at least 90% of its configured duration elapsed without a confirmed sensor exit, or the frame has no exit condition configured
- **THEN** the marker SHALL record `transitionReason = "time"`

#### Scenario: Firmware-skipped first frame carries no reason

- **WHEN** the machine reports a non-zero frame before the app ever observed frame 0 (no previous frame known)
- **THEN** the marker's `transitionReason` SHALL be empty

### Requirement: Unconfirmed reasons render as their sensor equivalent in displays

The live frame-transition pill, the shot graph marker suffixes (in-app and history), and the ShotServer web graph marker suffixes SHALL render `pressure_unconfirmed` identically to `pressure` and `flow_unconfirmed` identically to `flow` (same text, letter, and color). Display surfaces SHALL degrade gracefully on unknown reason values (no suffix / generic transition label), never blank or broken markers.

#### Scenario: Pill on an unconfirmed pressure exit

- **WHEN** a frame change is announced with `transitionReason = "pressure_unconfirmed"`
- **THEN** the transition pill SHALL show the "Pressure exit" text and pressure color

#### Scenario: Graph marker suffix on an unconfirmed flow exit

- **WHEN** a phase marker with `transitionReason = "flow_unconfirmed"` is rendered on the in-app or web shot graph
- **THEN** the marker label SHALL carry the `[F]` suffix

#### Scenario: Unknown future value degrades gracefully

- **WHEN** a marker carries a reason string that no display switch recognizes
- **THEN** the marker SHALL render with its plain label (no suffix) and the pill SHALL show the generic transition label

### Requirement: Persisted reason strings pass through unchanged

Shot history persistence, serialization, comparison, and AI-summary consumers SHALL store and forward the `transitionReason` string verbatim without normalizing or remapping values, so previously recorded data (guessed `pressure`/`flow` from before PR #1421, `time` from the interim window, empty pre-feature values) retains its recorded meaning.

#### Scenario: Old shot loads with its recorded reason

- **WHEN** a shot recorded before this capability is loaded from history
- **THEN** its markers SHALL expose the originally recorded `transitionReason` values unchanged
