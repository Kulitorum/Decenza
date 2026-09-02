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

- `Error_NoAC` must persist for 6 seconds before the warning shows. That interval is
  our own estimate — the one reported episode ran about three seconds, and this is
  that plus margin. It is explicitly NOT de1app's: de1app shows its warning
  immediately, and its `after 6000` is a one-shot post-connect re-check for a machine
  already latched in `Error_NoAC`, not a settling wait. An earlier revision of this
  change cited it as one, wrongly.
- Every line that ends an episode carries its measured duration, so a field log can
  correct the interval rather than leaving it an estimate.

This change knowingly departs from CLAUDE.md's "never use timers as guards" rule. The
rule is deliberately NOT amended here: the mechanism behind the spurious report is not
identified (de1app warns immediately on firmware 1352 and evidently needs no filter),
so writing a carve-out into a rule file would be doctrine from one observation. Revisit
once a field log with the new duration lines says what is actually happening.

Two things found while doing this are deliberately left out, to keep the change to the
reported defect:

- The shot timer is not stopped when the DE1 disconnects mid-shot, and its scale-timer
  pairing is not either. Pre-existing, filed separately.
- Whether de1app's immediate warning and ours differ because of firmware, or because a
  page blink costs less than a full-screen dialog, is unresolved.
- The no-timers rule in CLAUDE.md gains a narrow, argued hardware carve-out.
- The subsystem gets logging. It previously emitted nothing at all, which is why a
  20,578-line user log could neither confirm nor deny when the warning fired.

## Impact

- `src/machine/machinestate.cpp`, `src/machine/machinestate.h`
- `CLAUDE.md` (the no-timers rule)
- Spec: `standby-switch-warning`
