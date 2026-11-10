# Spec Delta

## MODIFIED Requirements

### Requirement: Unconfirmed reasons render as their sensor equivalent in displays

The live frame-transition pill, the shot graph marker suffixes (in-app and history), and the browser graph marker suffixes SHALL render `pressure_unconfirmed` identically to `pressure` and `flow_unconfirmed` identically to `flow` (same text, letter, and color). Display surfaces SHALL degrade gracefully on unknown reason values (no suffix / generic transition label), never blank or broken markers.

#### Scenario: Pill on an unconfirmed pressure exit

- **WHEN** a frame change is announced with `transitionReason = "pressure_unconfirmed"`
- **THEN** the transition pill SHALL show the "Pressure exit" text and pressure color

#### Scenario: Graph marker suffix on an unconfirmed flow exit

- **WHEN** a phase marker with `transitionReason = "flow_unconfirmed"` is rendered on the in-app or web shot graph
- **THEN** the marker label SHALL carry the `[F]` suffix

#### Scenario: Unknown future value degrades gracefully

- **WHEN** a marker carries a reason string that no display switch recognizes
- **THEN** the marker SHALL render with its plain label (no suffix) and the pill SHALL show the generic transition label

