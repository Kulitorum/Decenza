## ADDED Requirements

### Requirement: Cold-machine maintenance SHALL be enabled on firmware that natively supports it

When the connected DE1 reports a firmware build that natively honors cold
maintenance requests, the system SHALL allow maintenance operations
(Transport Mode / air purge, and Descale) to start without first requiring the
machine to reach ready temperature. On firmware builds below that threshold, the
ready-temperature precondition SHALL remain in force. The threshold build SHALL
be confirmed against the actual firmware release before it is wired in.

#### Scenario: Cold start allowed on supported firmware

- **GIVEN** the connected machine reports a firmware build at or above the
  cold-maintenance threshold
- **AND** the machine is cold (preheating or heating)
- **WHEN** the user opens Transport Mode
- **THEN** the start action SHALL be available without waiting for ready
  temperature
- **AND** the machine SHALL enter the air-purge drain when started

#### Scenario: Ready gate retained on older firmware

- **GIVEN** the connected machine reports a firmware build below the
  cold-maintenance threshold
- **WHEN** the user opens Transport Mode on a cold machine
- **THEN** the ready-temperature precondition SHALL still apply

## RENAMED Requirements

- FROM: `### Requirement: Transport Mode SHALL require the machine to be ready before starting on current firmware`
- TO: `### Requirement: Transport Mode SHALL require the machine to be ready before starting on unsupported firmware`

## MODIFIED Requirements

### Requirement: Transport Mode SHALL require the machine to be ready before starting on unsupported firmware

The ready-temperature precondition for Transport Mode SHALL apply only when the
connected machine's firmware build is below the cold-maintenance threshold. On
firmware at or above the threshold — which honors cold maintenance natively — the
precondition SHALL NOT apply and the drain SHALL be startable from a cold
machine. This supersedes the unconditional ready gate introduced by
`add-maintenance-card`.

#### Scenario: Start is unavailable while the machine is heating

- **GIVEN** the machine is still preheating or heating on firmware below the threshold
- **WHEN** the user opens Transport Mode
- **THEN** the start action SHALL be unavailable
- **AND** the page SHALL indicate the machine must reach ready temperature first

#### Scenario: Start becomes available once ready

- **GIVEN** the machine has reached ready temperature
- **WHEN** the user views the Transport Mode prepare step
- **THEN** the start action SHALL be available

#### Scenario: Gate is firmware-conditional

- **GIVEN** the machine is cold
- **WHEN** the firmware build is below the threshold
- **THEN** the start action SHALL be unavailable until the machine is ready
- **WHEN** the firmware build is at or above the threshold
- **THEN** the start action SHALL be available while cold
