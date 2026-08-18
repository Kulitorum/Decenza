# standby-switch-warning Specification

## Purpose

Warns the user when the DE1's front standby switch is cutting AC power to the machine, so a
tablet showing a frozen or unresponsive UI is understood as "flip the switch" rather than a fault.

## Requirements

### Requirement: A live Error_NoAC substate shows a dismissible full-screen warning

While connected to the DE1 and its substate is `Error_NoAC` (the front standby switch is cutting
AC), and the connected DE1's firmware build number is 1337 or newer, the system SHALL show a
full-screen warning telling the user to push the switch on. The warning SHALL be dismissible by a
tap anywhere on it, returning to the page the user was on before it appeared.

On firmware older than 1337, the system SHALL NOT show this warning, because that firmware range
reports `Error_NoAC` unreliably.

#### Scenario: Standby switch cuts power on supported firmware

- **WHEN** a connected DE1 running firmware 1337 or newer reports substate `Error_NoAC`
- **THEN** the system shows the "push the switch on" warning

#### Scenario: Older firmware stays silent

- **WHEN** a connected DE1 running firmware older than 1337 reports substate `Error_NoAC`
- **THEN** the system does not show the warning

#### Scenario: Tapping the warning dismisses it

- **WHEN** the warning is shown and the user taps anywhere on it
- **THEN** the warning is dismissed and the system returns to the page shown before the warning
  appeared

### Requirement: The warning clears when power is restored

Once a shown warning's substate is no longer `Error_NoAC` — because the switch was flipped back,
or the DE1 disconnected — the system SHALL clear the warning and return to the page shown before
it appeared, without requiring the user to dismiss it.

#### Scenario: Power is restored while the warning is showing

- **WHEN** the warning is shown and the connected DE1's substate changes away from `Error_NoAC`
- **THEN** the warning clears automatically and the system returns to the prior page

#### Scenario: The DE1 disconnects while the warning is showing

- **WHEN** the warning is shown and the DE1 connection is lost
- **THEN** the warning clears; a stale `Error_NoAC` value SHALL NOT persist across the
  disconnect and cannot re-show the warning on reconnect unless the machine reports it again

### Requirement: The warning is not tied to a specific skin or page

The warning SHALL be driven by the DE1's substate regardless of which layout, skin, or page the
user is currently viewing, so it appears consistently rather than only from specific screens.

#### Scenario: Warning appears regardless of current page

- **WHEN** `Error_NoAC` becomes the live substate while the user is on any page of the app
- **THEN** the warning appears
