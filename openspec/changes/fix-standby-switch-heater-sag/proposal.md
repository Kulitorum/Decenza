# Suppress the standby-switch warning when the machine was heating

## Why

The full-screen "Push the switch on" warning fires on machines whose front standby
switch is on and whose AC is plainly present. A user on firmware v1363 — inside the
range the 1337 gate treats as trustworthy — sees it on every tap-to-wake, and after
an app restart. It stays up about three seconds and clears itself with no action
taken; the machine works normally either side of it.

The DE1 reports substate `Error_NoAC` for those few seconds while it wakes or heats.
A snapshot cannot tell that apart from the real thing: an open switch reports
"Idle, Error_NoAC" and so does the transient — de1app says so explicitly, noting the
switch "only moves the SUBstate (e.g. \"Idle, ready\" => \"Idle, Error_NoAC\")".

So duration is the only discriminator. There is no event to key on instead: the DE1
pushes STATE_INFO on change, so nothing arrives to say a condition is still true. An
earlier attempt keyed on the preceding substate — event-based, and wrong, because the
substate an episode arrives from differs per entry point (`Ready` on a tap-to-wake, a
heating substate mid warm-up, nothing at all on a fresh connect), so it missed the
commonest path.

Neither reference app supports the current behaviour. Decaid decodes substate 217 and
surfaces it nowhere. de1app had the check disabled from 2024-09-01 for spurious
reporting, and now waits 6 s before believing the signal. Decenza gave it a blocking
full-screen takeover with no wait at all.

## What changes

- `Error_NoAC` must persist for 6 seconds before the warning shows. The interval is
  de1app's own figure: Decent write the firmware that emits this substate.
- The no-timers rule in CLAUDE.md gains a narrow, argued hardware carve-out.
- The subsystem gets logging. It previously emitted nothing at all, which is why a
  20,578-line user log could neither confirm nor deny when the warning fired.

## Impact

- `src/machine/machinestate.cpp`, `src/machine/machinestate.h`
- `CLAUDE.md` (the no-timers rule)
- Spec: `standby-switch-warning`
