# machine-maintenance Specification

## Purpose
Covers the machine-maintenance surface in Decenza: a Maintenance card on the Settings → Machine tab that gathers machine upkeep operations in one place. It is the single home for the Descaling Wizard (moved off the Profiles page) and the new Transport Mode, which drains the machine's internal water system for storage or transport by driving the DE1 into the firmware air-purge state. It also defines the Transport machine phase that maps to air purge, its auto-sleep suppression, and the firmware-driven requirement that the machine be ready before a drain can start.

## Requirements
### Requirement: The system SHALL present a Maintenance card on the Machine settings tab

The Settings → Machine tab SHALL include a Maintenance card positioned directly
below the Shot Map card. The card SHALL follow the existing card grammar
(`Theme.cardBackgroundColor`, `Theme.cardRadius`) and SHALL list machine
maintenance operations, each launching a full-screen guided page. At minimum the
card SHALL offer **Descaling Wizard** and **Transport Mode**. Each operation row
SHALL be a fully accessible control (role, name, focusable, press action) and any
emoji SHALL be rendered as an image and paired with a word, never as the sole
carrier of meaning.

#### Scenario: Maintenance card appears under Shot Map

- **WHEN** the user opens Settings → Machine
- **THEN** a Maintenance card SHALL be visible immediately below the Shot Map card
- **AND** it SHALL list a Descaling Wizard row and a Transport Mode row

### Requirement: The Descaling Wizard SHALL launch from the Maintenance card and no longer from the Profiles page

The Descaling Wizard SHALL be launched from the Maintenance card's Descaling
Wizard row (invoking the existing descaling navigation). The previous launch
button on the Profiles page (shown only in the Cleaning/Descale profile view)
SHALL be removed. The placeholder descale-wizard *profile* that existed solely to
surface the wizard in the profile list — a step-less profile whose tap opened the
wizard — SHALL be removed along with its resource registrations and special-case
tap handler. Real cleaning profiles SHALL remain in the profile list unchanged.

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

### Requirement: Transport Mode SHALL require the machine to be ready before starting on current firmware

Because current firmware (builds 1333 / 1352) can silently drop an air-purge
request while the machine is still preheating or heating on GHC-fitted hardware,
Transport Mode SHALL only allow the drain to start once the machine has reached
ready temperature. The prepare step SHALL make the "wait until ready" expectation
clear and SHALL keep the start action unavailable until then. (Reliable
cold-machine starts are handled by a separate, on-hold change gated on a firmware
release.)

#### Scenario: Start is unavailable while the machine is heating

- **GIVEN** the machine is still preheating or heating
- **WHEN** the user opens Transport Mode
- **THEN** the start action SHALL be unavailable
- **AND** the page SHALL indicate the machine must reach ready temperature first

#### Scenario: Start becomes available once ready

- **GIVEN** the machine has reached ready temperature
- **WHEN** the user views the Transport Mode prepare step
- **THEN** the start action SHALL be available
