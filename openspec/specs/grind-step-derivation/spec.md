# grind-step-derivation Specification

## Purpose

Governs how the grind and RPM step for a grinder is derived. The step is computed from the live shot history on each call rather than from the distinct-value cache, and consumers of history are notified only on writes.

## Requirements
### Requirement: The grind step SHALL be derived from the live database, not a cache
`grindStepForGrinder()` and `grindRpmStepForGrinder()` SHALL query the shot history directly on each call. They SHALL NOT read, populate or depend on the async distinct-value cache, or on `distinctCacheReady()`. The derivation MAY run inline on the calling thread, because it is small, bounded and runs on a discrete user action.

#### Scenario: A cache invalidation does not change the answer

- **GIVEN** a grinder whose history supports a step of `0.25`
- **WHEN** the distinct-value cache is invalidated
- **THEN** the next read SHALL still return `0.25`, with no intervening signal and no re-request

#### Scenario: The value is correct on the first read after startup

- **GIVEN** a freshly initialized storage
- **WHEN** the step is read immediately, with no waiting for background work
- **THEN** it SHALL return the derived step, not `0` meaning "not loaded yet"

### Requirement: The widget and the AI payload SHALL agree by construction

The step shown in the grind picker and the `stepSize` reported in `dialing_get_context` SHALL come
from the same function over the same rows, so they cannot diverge.

#### Scenario: Both paths report the same step

- **GIVEN** a grinder whose history supports a step of `0.25`
- **WHEN** the widget reads `grindStepForGrinder()` and the payload reads
  `queryGrinderContext().stepSize`
- **THEN** both SHALL be `0.25`

### Requirement: A write SHALL notify anything deriving from history
A shot saved, deleted, metadata-edited, or database imported SHALL emit `historyDataChanged()`, and SHALL do so on writes ONLY, never on completion of a read. Consumers SHALL NOT hold a history-derived value in a resident eager binding; they SHALL derive it inside the snapshot or refresh that consumes the value.

#### Scenario: A write moves the derived step

- **GIVEN** a grinder whose history supports a step of `0.5`
- **WHEN** shots are written that introduce a repeated `0.25` gap
- **THEN** the next read SHALL return `0.25`

#### Scenario: A suggestion getter is not called from a per-keystroke binding

- **GIVEN** a text field whose `suggestions` are drawn from shot history
- **WHEN** the user types a character
- **THEN** no database query SHALL run as a result
- **AND** the list SHALL be refreshed instead on load, on `historyDataChanged()`, or when the
  parameter it is scoped to is committed

### Requirement: Model matching SHALL fold case and surrounding whitespace

The grinder model SHALL be matched case-insensitively and with leading/trailing whitespace
trimmed, so a differently-typed name does not read as a grinder with no history.

#### Scenario: Differently-typed model names resolve to the same history

- **GIVEN** history recorded under grinder model `Zero`
- **WHEN** the step is read for `"  zero  "` or `"ZERO"`
- **THEN** each SHALL return the same step as `"Zero"`

### Requirement: An empty model SHALL pool the full cross-grinder history

An empty model means "no grinder selected". The ShotServer `/beans` form depends on this, since a
new bag has no equipment chosen yet. Shots with no equipment row at all SHALL be included.

#### Scenario: Equipment-less shots contribute to the empty-model step

- **GIVEN** shots with no equipment row stepping by `0.1`, alongside a `Zero` history stepping by
  `0.25`
- **WHEN** the step is read for the empty model
- **THEN** it SHALL be `0.1`
- **AND** the step for `"Zero"` SHALL still be `0.25`

### Requirement: A history too thin to derive SHALL report zero

A grinder with fewer than two distinct numeric settings SHALL return `0`. `0` means "cannot
derive" and the caller substitutes its own fallback; it is not a step of zero.

#### Scenario: One distinct setting defines no step

- **GIVEN** a grinder with exactly one distinct numeric setting
- **WHEN** the step is read
- **THEN** it SHALL be `0`

