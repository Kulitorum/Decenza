# Suppress the standby-switch warning when the machine was heating

## Why

The full-screen "Push the switch on" warning is firing on machines whose front
standby switch is on and whose AC is plainly present. A user on firmware v1363 —
inside the range the 1337 gate treats as trustworthy — reported it appearing
seconds after the app wakes and about a minute after a restart, staying up for
about three seconds and clearing itself with no action taken. The machine was
heating each time and worked normally either side of it.

The DE1 reports substate `Error_NoAC` for those few seconds while it is heating.
The heater masking the firmware's own mains zero-cross detection is the obvious
cause, but the mechanism does not have to be settled to act on it: a machine that
reached a heating substate has AC, so an `Error_NoAC` episode that begins from one
is not the standby switch.

Neither reference app supports the current behaviour. Decaid decodes substate 217
and surfaces it nowhere. de1app had the check disabled from 2024-09-01 for
spurious reporting, and now renders it as a page chosen by the Idle mapping, with
a dismissal latch and an `after 6000` settle before the signal is believed.
Decenza gave the same noisy signal a blocking full-screen takeover with neither.

## What changes

- An `Error_NoAC` episode that begins from a heating substate is not treated as
  the standby switch, for the whole episode.
- The subsystem gets logging. It previously emitted nothing at all, which is why
  a 20,578-line user log could neither confirm nor deny when the warning fired.

## Impact

- `src/machine/machinestate.cpp`, `src/machine/machinestate.h`
- Spec: `standby-switch-warning`
