# standby-switch-warning Specification

## Purpose

Warns the user when the DE1's front standby switch is cutting AC power to the machine, so a
tablet showing a frozen or unresponsive UI is understood as "flip the switch" rather than a fault.

## Requirements

### Requirement: A live Error_NoAC substate shows a dismissible full-screen warning

While connected to the DE1 and its substate is `Error_NoAC` (the standby switch cutting AC), on firmware build 1337 or newer, the system SHALL show a full-screen warning telling the user to push the switch on. A tap anywhere on it SHALL dismiss it, returning to the page shown before. On firmware older than 1337 the system SHALL NOT show the warning, because that range reports `Error_NoAC` unreliably.

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

### Requirement: The warning waits out a settling interval

The warning SHALL NOT show until `Error_NoAC` has persisted continuously for a 6-second settling interval, because firmware in the supported range also reports it briefly while the machine wakes or heats. An episode that clears before the interval elapses SHALL never show the warning.

#### Scenario: Interval is a fixed six seconds
- **WHEN** the settling interval is configured
- **THEN** it is 6 seconds, an estimate from one observed episode of about three seconds plus margin, not a figure taken from another implementation

### Requirement: Settling is judged on duration alone

Settling SHALL be judged on duration alone, not on the substate the episode arrived from, because a snapshot reports state `Idle` with substate `Error_NoAC` whichever entry point produced it.

#### Scenario: Arrival substate does not change settling
- **WHEN** an `Error_NoAC` episode arrives from a different substate than a previous episode
- **THEN** it is settled by duration exactly as the previous one was

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

### Requirement: The warning's decisions are logged

The system SHALL log under the DE1 subsystem, at INFO, when the warning is shown, when it clears, and once per episode that ends before the settling interval elapses. Every line that ENDS an episode, whether it cleared itself or the DE1 disconnected, SHALL carry the episode's measured duration and the configured interval.

#### Scenario: A shown warning is traceable in a submitted log

- **WHEN** the warning is shown and later clears
- **THEN** the log carries an INFO line for each, and the episode's measured duration appears on
  the line that ends the episode

#### Scenario: An episode ended by a disconnect is still measured

- **WHEN** the DE1 disconnects while an `Error_NoAC` episode is running
- **THEN** the log carries an INFO line naming the episode's measured duration, so the
  measurement is not lost

#### Scenario: A self-clearing episode is traceable

- **WHEN** an `Error_NoAC` episode clears before the settling interval elapses
- **THEN** the log carries one INFO line naming the substate the machine moved to, the episode's
  measured duration, and the configured interval
