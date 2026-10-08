## REMOVED Requirements

### Requirement: Transport Mode SHALL require the machine to be ready before starting on current firmware
**Reason**: The shared maintenance handler already supports cold requests, including preparation for older/unknown GHC firmware; the page's unconditional temperature gate prevents that supported path.
**Migration**: Use the connection/operation eligibility and shared-handler requirement below.

## ADDED Requirements

### Requirement: Transport start SHALL use connection and operation eligibility

Transport Mode SHALL allow starting on a connected machine in idle/heating/ready states, without waiting for Ready. Cold requests SHALL use the existing shared maintenance preparation for older or unknown GHC firmware and request AirPurge directly on native-supporting firmware. Simulation SHALL retain its bypass. Sleep, disconnection and other operations SHALL keep the real-machine Start action unavailable.

#### Scenario: Heating alone does not block a connected idle machine
- **GIVEN** a connected machine is idle/heating and not running another operation
- **WHEN** the user opens Transport Mode
- **THEN** Start SHALL be available before Ready

#### Scenario: Start becomes available once ready
- **GIVEN** the connected machine has reached ready temperature
- **WHEN** the user views the Transport prepare step
- **THEN** Start SHALL be available

#### Scenario: Cold old or unknown GHC firmware uses preparation
- **GIVEN** a connected idle/heating GHC machine with old or unknown firmware
- **WHEN** the user starts Transport
- **THEN** the existing shared maintenance handler SHALL prepare the machine and send AirPurge after it leaves preheat

#### Scenario: Supported firmware starts cold directly
- **GIVEN** a connected idle/heating machine on native-supporting firmware
- **WHEN** the user starts Transport
- **THEN** AirPurge SHALL be requested without a preparation profile

#### Scenario: Ineligible machine cannot start
- **GIVEN** a real machine is asleep, disconnected, or running another operation
- **WHEN** the user views the prepare step
- **THEN** Start SHALL be unavailable and the hint SHALL explain connection/wake/operation eligibility

#### Scenario: Simulation retains its start behavior
- **WHEN** the user opens Transport Mode in simulation
- **THEN** the connection/temperature gate SHALL remain bypassed

### Requirement: Transport exit restores the selected brew profile when idle

After leaving Transport in an idle/heating/ready phase, the app SHALL restore the selected brew profile through its normal upload path. It SHALL NOT upload a brew profile over another active operation.

#### Scenario: Returning after a prepared cold drain
- **GIVEN** the shared handler installed a temporary cold-maintenance profile
- **WHEN** the user leaves Transport while the machine is idle/heating/ready
- **THEN** the selected brew profile SHALL be uploaded

#### Scenario: Another operation replaces Transport
- **WHEN** another active operation replaces Transport
- **THEN** Transport's exit SHALL NOT upload a brew profile over that operation
