# Spec Delta

## MODIFIED Requirements

### Requirement: An empty model SHALL pool the full cross-grinder history

An empty model means "no grinder selected". Shots with no equipment row at all SHALL be included.

#### Scenario: Equipment-less shots contribute to the empty-model step

- **GIVEN** shots with no equipment row stepping by `0.1`, alongside a `Zero` history stepping by
  `0.25`
- **WHEN** the step is read for the empty model
- **THEN** it SHALL be `0.1`
- **AND** the step for `"Zero"` SHALL still be `0.25`

