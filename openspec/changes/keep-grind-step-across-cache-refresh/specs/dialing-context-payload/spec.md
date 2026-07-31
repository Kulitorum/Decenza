## MODIFIED Requirements

### Requirement: grinderContext SHALL report the grinder's smallest commonly-repeated step

`grinderContext` SHALL carry a `stepSize` field giving the grinder's effective dial step — the smallest increment the user makes repeatedly between observed settings. The estimator SHALL be a single shared helper (`deriveGrindStep`) used by both the `dialing_get_context` MCP payload, the in-app AI user-prompt enrichment, AND the Grind quick-select widget, so all surfaces report the same value.

Sharing the *estimator* is not sufficient and SHALL NOT be relied on alone: the payload and the widget previously reached `deriveGrindStep` through two separate queries, and the two agreed only by convention while one of them was silently failing. They SHALL therefore share the **derivation itself** — one pass over the history whose result every surface reads (see `grind-step-derivation`) — so a divergence is not expressible rather than merely unintended. The payload SHALL NOT issue its own query for this value.

The estimator SHALL operate on the sorted, de-duplicated numeric settings and return the **smallest gap that occurs at least twice** between consecutive values (rounding gaps to absorb floating-point noise), falling back to the smallest gap when none repeats, and clamping to a sane floor. This rejects both a lone mistyped setting (its gap occurs once and is skipped) and the coarse bias of a most-common-gap approach (which would hide a fine step the user makes less often than a coarse one). `stepSize` SHALL be present only when at least two distinct numeric settings are available; otherwise it SHALL be omitted.

The step SHALL be computed **grinder-model-wide across all beans and beverages** — it is a property of the grinder, not the bean or the drink — so the widget and the AI payload derive it from the same scope and cannot diverge (`settingsObserved`, min, and max remain bean-scoped, as per-bean context; only `stepSize` is grinder-wide).

This replaces the former `smallestStep` field, which reported the raw minimum gap and was therefore collapsed to a spurious value by a single mistyped setting.

#### Scenario: Step from clean history

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be `0.25`
- **AND** the payload SHALL NOT carry a `smallestStep` field

#### Scenario: Payload and widget cannot disagree

- **GIVEN** a grinder whose history derives a step of `0.25`
- **WHEN** `grinderContext.stepSize` and the grind picker's step are both obtained
- **THEN** both SHALL be `0.25`
- **AND** both SHALL have read the same derivation rather than issuing separate queries

#### Scenario: The finest repeated step wins over a more common coarse one

- **GIVEN** the grinder's observed settings are 5, 5.5, 6, 7, 7.5, 8, 8.5, 8.75, 9, 10, 12 (five 0.5 gaps, two 0.25 gaps)
- **WHEN** `grinderContext.stepSize` is derived
- **THEN** it SHALL be `0.25` (the finest repeated step), not `0.5` (the most common gap)

#### Scenario: A lone outlier does not collapse the step

- **GIVEN** the grinder's observed numeric settings are 7.5, 8, 8.5, 8.75, 9 plus a single `8.1`
- **WHEN** `grinderContext.stepSize` is derived
- **THEN** it SHALL remain `0.25`, not `0.1`

#### Scenario: Insufficient data omits the step

- **GIVEN** the grinder has fewer than two distinct numeric settings in history
- **WHEN** `grinderContext` is built
- **THEN** `grinderContext.stepSize` SHALL be omitted
