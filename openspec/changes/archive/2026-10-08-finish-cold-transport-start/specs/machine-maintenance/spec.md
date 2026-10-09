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

#### Scenario: GHC status has not been read
- **GIVEN** the machine is heating and firmware is old or unknown
- **AND** GHC hardware status is still unconfirmed
- **WHEN** the user starts Transport
- **THEN** cold preparation SHALL be used conservatively
- **AND** confirmed no-GHC hardware SHALL retain its direct start path

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

Leaving or covering Transport SHALL request restoration of the selected brew profile through its normal upload path. Restoration SHALL wait until the device is idle and the UI phase is idle/heating/ready. It SHALL NOT write a brew profile or shot settings over another active operation, including steam warm-up displayed as Heating.

#### Scenario: Returning after a prepared cold drain
- **GIVEN** the shared handler installed a temporary cold-maintenance profile
- **WHEN** the user leaves Transport while the machine is idle/heating/ready
- **THEN** the selected brew profile SHALL be uploaded

#### Scenario: Another operation replaces Transport
- **WHEN** another active operation replaces Transport
- **THEN** Transport's exit SHALL queue profile restoration without writing over that operation
- **AND** the selected brew profile SHALL be restored once the device returns to idle

#### Scenario: Another page covers Transport during preparation
- **GIVEN** cold preparation installed the temporary maintenance profile
- **WHEN** Settings or History covers Transport without destroying it
- **THEN** the deferred start SHALL be cancelled and the selected profile restored while the device is idle

#### Scenario: Steam warm-up replaces Transport
- **WHEN** Steam replaces Transport while the UI reports Heating
- **THEN** profile restoration SHALL remain deferred until the device is idle

### Requirement: Leaving Transport cancels a deferred cold start

Leaving or covering Transport during cold preparation SHALL cancel its deferred AirPurge before navigation/teardown can finish. A later ready notification SHALL NOT revive that request. Cancellation SHALL be idempotent, preserve other pending maintenance operations, and allow a later explicit Transport start. Losing the connection SHALL discard deferred maintenance requests belonging to it.

#### Scenario: Back during cold preparation
- **GIVEN** an old or unknown-firmware GHC machine has a deferred AirPurge
- **WHEN** the user leaves Transport through Back, system navigation or another page
- **THEN** the deferred AirPurge SHALL be cancelled before profile restoration
- **AND** a later ready notification SHALL NOT start that cancelled purge

#### Scenario: Group head starts a competing operation
- **GIVEN** a deferred AirPurge is waiting for cold preparation
- **WHEN** the device reports another operation, even in a heating substate
- **THEN** the deferred purge SHALL be cancelled before state observers run
- **AND** a later ready notification SHALL NOT revive it

#### Scenario: Repeated cancellation or another operation
- **WHEN** Transport exit is handled more than once, or another maintenance request is pending
- **THEN** Transport cancellation SHALL be a no-op for requests other than its deferred AirPurge

#### Scenario: Later explicit start
- **GIVEN** the deferred Transport request was cancelled
- **WHEN** the user later explicitly starts Transport while eligible
- **THEN** the new AirPurge request SHALL be accepted normally

#### Scenario: Connection lost during preparation
- **GIVEN** a maintenance request is deferred on the current connection
- **WHEN** that connection is lost
- **THEN** the deferred request SHALL be discarded
- **AND** later state notifications SHALL NOT start it
