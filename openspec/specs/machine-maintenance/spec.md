# machine-maintenance Specification

## Purpose
Covers the machine-maintenance surface in Decenza: a Maintenance card on the Settings → Machine tab that gathers machine upkeep operations in one place. It is the single home for the Descaling Wizard (moved off the Profiles page) and the new Transport Mode, which drains the machine's internal water system for storage or transport by driving the DE1 into the firmware air-purge state. It also defines the Transport machine phase that maps to air purge, its auto-sleep suppression, and connection/operation eligibility for cold starts through the shared maintenance handler.

## Requirements

### Requirement: The system SHALL present a Maintenance card on the Machine settings tab

The Settings → Machine tab SHALL include a Maintenance card directly below the Shot Map card, following the existing card grammar (`Theme.cardBackgroundColor`, `Theme.cardRadius`). The card SHALL list machine maintenance operations, each launching a full-screen guided page, and at minimum SHALL offer **Descaling Wizard** and **Transport Mode**.

#### Scenario: Maintenance card appears under Shot Map

- **WHEN** the user opens Settings → Machine
- **THEN** a Maintenance card SHALL be visible immediately below the Shot Map card
- **AND** it SHALL list a Descaling Wizard row and a Transport Mode row

### Requirement: Maintenance rows are accessible and never icon-only

Each operation row SHALL be a fully accessible control with a role, a name, focus, and a press action. Any emoji SHALL be rendered as an image and paired with a word, never as the sole carrier of meaning.

#### Scenario: Row is exposed to assistive technology

- **WHEN** a screen reader inspects a maintenance row
- **THEN** it SHALL expose a button role, the operation's name, focusability, and a press action

### Requirement: The Descaling Wizard SHALL launch from the Maintenance card and no longer from the Profiles page

The Descaling Wizard SHALL be launched from the Maintenance card's Descaling Wizard row, invoking the existing descaling navigation. The Profiles page launch button (Cleaning/Descale view) SHALL be removed. The placeholder descale-wizard profile, a step-less profile whose tap opened the wizard, SHALL be removed along with its resource registrations and special-case tap handler. Real cleaning profiles SHALL remain unchanged.

#### Scenario: Descale launches from Maintenance

- **WHEN** the user taps Descaling Wizard on the Maintenance card
- **THEN** the existing Descaling page SHALL open and function as before

#### Scenario: Descale button removed from Profiles

- **WHEN** the user opens the Profiles page and selects the Cleaning/Descale view
- **THEN** no Descaling Wizard button SHALL be shown there
- **AND** no placeholder descale-wizard profile SHALL appear in the list

#### Scenario: Real cleaning profiles are unaffected

- **WHEN** the user opens the Cleaning/Descale profile view
- **THEN** the actual cleaning profiles (Forward Flush, Weber Spring Clean, …)
  SHALL still be listed and loadable

### Requirement: The system SHALL provide a Transport Mode that drains the machine for storage or transport

Transport Mode SHALL guide the user through emptying the machine's internal water
system by driving the DE1 into the firmware `AirPurge` state (`0x14`). The guided
page SHALL present three stages: a prepare step with instructions (including
pulling the water tank forward when prompted), a running step that reflects drain
progress, and a completion step confirming the machine is empty and safe to power
off. Leaving the page SHALL restore normal operation.

#### Scenario: User runs Transport Mode to completion

- **GIVEN** the machine has reached ready temperature
- **WHEN** the user starts Transport Mode and follows the prompts
- **THEN** the machine SHALL enter the air-purge drain
- **AND** on completion the page SHALL confirm the machine is empty and can be
  powered off for transport

### Requirement: Air purge SHALL surface as a Transport machine phase that suppresses auto-sleep

The `AirPurge` firmware state SHALL map to a dedicated `Transport` machine phase.
While the machine is in the Transport phase, the app's auto-sleep inactivity
countdown SHALL be paused (the phase SHALL be part of the active-operation set),
so a multi-minute drain cannot be interrupted by auto-sleep — consistent with the
existing treatment of the Descaling and Cleaning phases.

#### Scenario: Auto-sleep does not fire during a drain

- **GIVEN** Transport Mode is running and the machine is in the Transport phase
- **WHEN** the auto-sleep inactivity interval would otherwise elapse
- **THEN** the app SHALL NOT send the machine to sleep while the drain is in
  progress

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
