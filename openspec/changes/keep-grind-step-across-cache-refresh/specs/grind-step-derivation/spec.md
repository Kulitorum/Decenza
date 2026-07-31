# Grind Step Derivation

## MODIFIED Requirements

### Requirement: The grind step SHALL be derived from the live database, not a cache

`grindStepForGrinder()` and `grindRpmStepForGrinder()` SHALL query the shot history directly on
each call. They SHALL NOT read, populate, or depend on the async distinct-value cache, and SHALL
NOT depend on `distinctCacheReady()` to deliver a correct answer.

The derivation is small and bounded and runs on a discrete user action (the grind picker opening),
so it MAY run inline on the calling thread. Measured: 3.3 ms median / 87 ms worst on a real
18.5 MB database, 37 ms median / 41 ms worst on a 16× copy.

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
