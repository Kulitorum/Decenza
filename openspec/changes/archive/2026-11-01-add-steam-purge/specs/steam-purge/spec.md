## Purpose

Lets the user run the DE1's firmware steam purge (SteamRinse) from the app: on the Steam page right after steaming, and from the idle page's steam row.

## ADDED Requirements

### Requirement: Purge requests the firmware steam purge
A Purge control SHALL request the DE1's SteamRinse state. The app SHALL allow it whenever the machine is ready, including when the GHC is active. The firmware ends a purge by itself; the app SHALL stop one only on Back from the post-steam window or an MCP `machine_stop`.

#### Scenario: Purge on a machine with an active GHC
- **GIVEN** a ready machine whose group head controller is active
- **WHEN** the user taps Purge
- **THEN** the app requests SteamRinse without a GHC tap

#### Scenario: Purge is not offered mid-operation
- **WHEN** the machine is steaming, pouring, or running any other operation
- **THEN** a Purge tap starts nothing

### Requirement: The app shows a purge while it runs
While the DE1 reports SteamRinse, or reports Steam for the app's current purge request, the app SHALL report the phase `SteamPurge`: the machine SHALL NOT count as ready, other start controls SHALL be unavailable, and status surfaces (home-screen widget, MQTT, MCP) SHALL name it. A purge SHALL NOT open an operation page or count as a steam session.

#### Scenario: Purging blocks other starts
- **WHEN** a purge is running
- **THEN** espresso, steam, hot water and flush cannot be started from the app

#### Scenario: Purge is not a steam session
- **WHEN** a purge runs and ends
- **THEN** no steam-session finalisation, steam health record or milk readout is produced

#### Scenario: Firmware reports Steam for a purge
- **WHEN** the app requests SteamRinse and the machine reports Steam
- **THEN** the app reports SteamPurge and preserves the current page

### Requirement: Failed and cancelled purge requests do not capture later steam
The app SHALL clear unacknowledged purge intent when its state write is abandoned or discarded. An explicit Idle cancellation SHALL clear purge intent when Idle is reported, even if Steam was never observed. Acknowledged or observed purges SHALL retain their identity through unrelated queue cleanup and late Steam replies while cancellation is pending.

#### Scenario: Purge write never reaches the machine
- **WHEN** a purge write is abandoned or discarded, then the user starts steam from the GHC
- **THEN** the app handles the new operation as normal steaming

#### Scenario: Immediate Back cancels a purge
- **WHEN** Back requests Idle before the machine has reported the requested purge, then Idle is reported
- **THEN** a later GHC-initiated Steam is handled as normal steaming

### Requirement: The Steam page offers Purge after steaming
When steaming ends and the machine reports Idle, the Steam page SHALL stay up for 10 seconds showing Purge, Done and a bottom bar with Back, instead of leaving after the 1.5 second completion message. Purge SHALL start a purge and the page SHALL leave when the purge ends. Done SHALL leave immediately. Back SHALL stop a running purge or steam and leave immediately. Hot water and flush completion SHALL keep their 1.5 second message.

#### Scenario: Purge from the post-steam window
- **WHEN** steaming ends and the user taps Purge within 10 seconds
- **THEN** the purge runs, the page shows it, and the page leaves when the machine returns to Idle

#### Scenario: Nothing tapped
- **WHEN** steaming ends and the user taps nothing
- **THEN** the Steam page leaves after 10 seconds

#### Scenario: Done
- **WHEN** steaming ends and the user taps Done
- **THEN** the Steam page leaves at once

#### Scenario: Back during a purge
- **WHEN** a purge started from the post-steam window is running and the user taps Back
- **THEN** the app requests Idle and the Steam page leaves at once

### Requirement: The idle steam row offers Purge
The idle page's steam row SHALL show a Purge pill after Heater off, in both its full and compact forms. One tap SHALL start a purge; the pill SHALL NOT be a selectable or editable pitcher preset. It SHALL meet the project's accessibility rules for interactive elements.

#### Scenario: Purge from the idle page
- **WHEN** the machine is ready and the user taps the Purge pill
- **THEN** a purge starts and the selected pitcher is unchanged

### Requirement: The simulator runs a purge
In simulation, a purge request SHALL put the simulated DE1 in SteamRinse for a few seconds and then return it to Idle by itself, whether or not the simulated GHC is active.

#### Scenario: Simulated purge ends by itself
- **WHEN** Purge is tapped with the DE1 simulator running
- **THEN** the simulated machine reports SteamRinse, then Idle without a stop request
