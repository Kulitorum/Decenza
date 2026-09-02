## MODIFIED Requirements

### Requirement: A live Error_NoAC substate shows a dismissible full-screen warning

While connected to the DE1 and its substate is `Error_NoAC` (the front standby switch is cutting
AC), and the connected DE1's firmware build number is 1337 or newer, the system SHALL show a
full-screen warning telling the user to push the switch on. The warning SHALL be dismissible by a
tap anywhere on it, returning to the page the user was on before it appeared.

On firmware older than 1337, the system SHALL NOT show this warning, because that firmware range
reports `Error_NoAC` unreliably.

The system SHALL NOT show the warning until the `Error_NoAC` substate has persisted
continuously for a settling interval, because firmware in the supported range also reports it
briefly while the machine wakes or heats and clears it with no user action. An episode that
clears before the interval elapses SHALL never show the warning. The interval SHALL match
de1app's, which is 6 seconds.

The settling SHALL be judged on duration alone, not on which substate the episode arrived from:
a snapshot cannot distinguish the two cases (both report state `Idle` with substate
`Error_NoAC`), and the arriving substate varies by entry point.

#### Scenario: Standby switch cuts power on supported firmware

- **WHEN** a connected DE1 running firmware 1337 or newer reports substate `Error_NoAC`
- **THEN** the system shows the "push the switch on" warning

#### Scenario: Older firmware stays silent

- **WHEN** a connected DE1 running firmware older than 1337 reports substate `Error_NoAC`
- **THEN** the system does not show the warning

#### Scenario: A brief Error_NoAC report clears itself

- **WHEN** a connected DE1 reports substate `Error_NoAC` and leaves it before the settling
  interval has elapsed
- **THEN** the system never shows the warning

#### Scenario: The report arrives from any substate

- **WHEN** a connected DE1 reports substate `Error_NoAC` immediately after any other substate,
  including `Ready` and each heating substate
- **THEN** the system does not show the warning until the settling interval has elapsed

#### Scenario: The DE1 disconnects while the interval is running

- **WHEN** the DE1 disconnects before the settling interval has elapsed
- **THEN** the interval is abandoned and the warning is not shown

#### Scenario: Tapping the warning dismisses it

- **WHEN** the warning is shown and the user taps anywhere on it
- **THEN** the warning is dismissed and the system returns to the page shown before the warning
  appeared

## ADDED Requirements

### Requirement: The warning's decisions are logged

The system SHALL log, under the DE1 subsystem, when the warning is shown and when it clears, and
SHALL log once per episode that clears before the settling interval elapses. All three SHALL be
at INFO so they reach the user-facing log views: the last is what answers "why did no warning
appear", which is the half a reader needs when the suppression is wrong.

#### Scenario: A shown warning is traceable in a submitted log

- **WHEN** the warning is shown and later clears
- **THEN** the log carries an INFO line for each, naming the machine state and firmware build

#### Scenario: A self-clearing episode is traceable

- **WHEN** an `Error_NoAC` episode clears before the settling interval elapses
- **THEN** the log carries one INFO line naming the substate the machine moved to
