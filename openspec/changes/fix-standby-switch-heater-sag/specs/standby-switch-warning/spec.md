## MODIFIED Requirements

### Requirement: A live Error_NoAC substate shows a dismissible full-screen warning

While connected to the DE1 and its substate is `Error_NoAC` (the front standby switch is cutting
AC), and the connected DE1's firmware build number is 1337 or newer, the system SHALL show a
full-screen warning telling the user to push the switch on. The warning SHALL be dismissible by a
tap anywhere on it, returning to the page the user was on before it appeared.

On firmware older than 1337, the system SHALL NOT show this warning, because that firmware range
reports `Error_NoAC` unreliably.

A machine that has reached a heating substate has AC. So where an `Error_NoAC` episode begins
from a heating substate, the system SHALL NOT show the warning for that episode, on any firmware
build — firmware in the supported range reports `Error_NoAC` for a few seconds while heating, and
clears it with no user action. The suppression SHALL last until the substate leaves `Error_NoAC`,
so that a re-evaluation which sees the substate unchanged cannot lift it.

#### Scenario: Standby switch cuts power on supported firmware

- **WHEN** a connected DE1 running firmware 1337 or newer reports substate `Error_NoAC`
- **THEN** the system shows the "push the switch on" warning

#### Scenario: Older firmware stays silent

- **WHEN** a connected DE1 running firmware older than 1337 reports substate `Error_NoAC`
- **THEN** the system does not show the warning

#### Scenario: A heating machine blips into Error_NoAC

- **WHEN** a connected DE1 reports substate `Error_NoAC` immediately after a heating substate
- **THEN** the system does not show the warning, for as long as the substate stays `Error_NoAC`

#### Scenario: The firmware build number arrives during a suppressed episode

- **WHEN** an `Error_NoAC` episode is suppressed as heater-induced and the DE1's firmware build
  number then becomes known, re-evaluating the condition
- **THEN** the system still does not show the warning

#### Scenario: The condition recurs after the blip ends

- **WHEN** a suppressed episode ends, and the machine later reports `Error_NoAC` again from a
  non-heating substate
- **THEN** the system shows the warning

#### Scenario: Tapping the warning dismisses it

- **WHEN** the warning is shown and the user taps anywhere on it
- **THEN** the warning is dismissed and the system returns to the page shown before the warning
  appeared

## ADDED Requirements

### Requirement: The warning's decisions are logged

The system SHALL log, under the DE1 subsystem, when the warning is shown and when it clears, and
SHALL log once per episode that is suppressed as heater-induced. All three SHALL be at INFO so
they reach the user-facing log views: the suppressed line is what answers "why did no warning
appear", which is the half a reader needs when the suppression is wrong.

#### Scenario: A shown warning is traceable in a submitted log

- **WHEN** the warning is shown and later clears
- **THEN** the log carries an INFO line for each, naming the machine state and firmware build

#### Scenario: A suppressed episode is traceable

- **WHEN** an `Error_NoAC` episode is suppressed as heater-induced
- **THEN** the log carries one INFO line naming the substate it arrived from
