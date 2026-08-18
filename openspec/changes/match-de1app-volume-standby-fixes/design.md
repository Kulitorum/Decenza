## Context

Two independent fixes, bundled because both are direct ports of specific upstream de1app fixes
landed the same day. See proposal.md for motivation. Design-relevant current state:

- **SAV volume routing**: `MachineState::onFlowSample()` already buckets flow by `m_phase`
  (`Preinfusion` vs not), derived from the DE1 firmware's own `SubState` byte, which the firmware
  computes by comparing the live frame index against the `NumberOfPreinfuseFrames` header value
  Decenza uploads with the profile. Decenza never touches raw frame indices for this — the fix is
  entirely in what value Decenza puts in that header for a generated Pressure profile.
- **Substate 217**: `DE1::SubState` is a fixed enum Decenza `static_cast`s the raw STATE_INFO byte
  into; an unrecognized value (217 today) survives the cast but matches no case, so it silently
  falls through wherever consumers switch on it. `m_subState` is never reset in
  `DE1Device::onTransportDisconnected()`.
- **Modal warning precedent**: the refill dialog (`qml/main.qml`) is the existing pattern for a
  machine-driven, skin-independent, full-screen condition: a `Connections` block on
  `MachineState`'s signal opens/closes a `Dialog.NoAutoClose` at the shell level, not from a
  StackView page. This sidesteps de1app's original bug (`check_front_switch` living inside a
  skin-specific per-redraw textvariable that most skins never evaluated) by construction, since
  `main.qml`'s `Connections` block runs regardless of which page/skin is active.

## Goals / Non-Goals

**Goals:**
- Make the DE1's own preinfusion-substate mechanism report forced-rise frames correctly, so no
  new client-side volume-accounting logic is needed.
- Make the no-AC warning a shell-level reaction to a signal, not a page- or skin-specific redraw
  path, avoiding de1app's page-ordering flicker bug structurally rather than by event-ordering
  hacks.

**Non-Goals:**
- Not changing SAV's volume-accumulation logic itself (`checkStopAtVolume()`) — the fix is upstream
  of it, in what frames get labeled preinfusion.
- Not touching Advanced/imported profiles with a stored, unregenerated `advanced_shot` — those are
  already out of scope per the existing "Simple profiles derive frames from their scalars"
  requirement.
- Not adding a settings toggle for the no-AC warning; de1app doesn't offer one and the condition
  is unambiguous (the machine cannot make coffee at all while it holds).

## Decisions

**Count forced-rise frames via the same derivation path that already sets the header value.**
`Profile::countPreinfuseFrames()` counts leading frames with `exitIf==true` and stops at the first
`exitIf==false` frame — which is exactly the forced-rise frame `RecipeGenerator::generatePressureFrames()`
emits, so today it's excluded by construction. Two ways to fix this: (a) mark the forced-rise
frame(s) `exitIf==true` so the existing leading-run scan naturally includes them, or (b) have
`generatePressureFrames()` increment a separate preinfusion-count field the way de1app's
`incr temp_advanced(final_desired_shot_volume_advanced_count_start)` does. Prefer (a) if
`exitIf==true` on a forced-rise-without-limit frame doesn't change its DE1-side exit behavior
(worth confirming against the frame's actual exit-condition semantics during implementation);
otherwise fall back to (b), which mirrors de1app's own approach and is guaranteed safe since it
only changes the header value, not the frame's own exit fields.

**Gate the warning on firmware build number, matching de1app's threshold exactly (1337).**
Decenza already exposes `DE1Device::firmwareBuildNumber()`. No reason to pick a different
threshold than the one de1app verified against real hardware.

**Model the warning as a shell-level modal via `MachineState`, not a StackView page.**
Matches the refill-dialog precedent, keeps `QML_NAVIGATION.md`'s "machine drove it → shell
reacts" rule, and is immune to de1app's per-skin/per-page reachability bug: there is only one
shell (`main.qml`), so no analog of "Insight and Streamline never call this" can exist here.
`MachineState` gains an `Error_NoAC`-derived signal/property (e.g. a `standbySwitchOpen` bool)
that `main.qml` observes the same way it observes refill state.

**Dismiss returns to "whatever was showing," not a fixed page.** Since this is a modal dialog
over the existing StackView rather than a page replacement, "return to prior page" is automatic —
closing the dialog just reveals what was already underneath. This avoids de1app's need to track
and restore a `return_page` variable by hand.

## Risks / Trade-offs

- [Firmware misreports `Error_NoAC` below build 1337, same as de1app's original spurious reports]
  → Mitigation: gate identical to de1app (`>= 1337`); do not attempt an independent lower bound.
- [A forced-rise frame marked `exitIf==true` unexpectedly changes DE1-side exit behavior for that
  frame, altering the shot beyond the intended header-value change] → Mitigation: verify frame
  exit semantics against `BLE_PROTOCOL.md`/firmware docs before choosing option (a) over (b) in
  Decisions above; prefer (b) if there's any doubt, since it cannot touch exit behavior.
- [Existing Pressure-profile regression/golden shot data in `tests/data/shots/` reflects the old
  (buggy) preinfusion count] → Mitigation: identify and update/regenerate any fixture whose
  expected preinfusion volume assumed the forced-rise frame was counted as pour, per
  `docs/CLAUDE_MD/TESTING.md`'s `shot_eval` corpus conventions.
