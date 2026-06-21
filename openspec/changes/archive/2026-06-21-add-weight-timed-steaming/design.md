## Context

The DE1 stops steaming purely by time (`TargetSteamLength`); it has no milk thermometer. Steam power is roughly constant, so time-to-target-temperature is approximately proportional to milk mass. A fixed steam timeout therefore over-heats small pours and under-heats large ones.

Building blocks already in `main`:
- Steam presets: JSON with `name`, `duration`, `flow`, optional `pitcherWeightG`, `disabled`.
- `MachineState.phase === Steaming` marks a session; `MachineState.shotTime` is the live elapsed seconds.
- `qml/components/StableWeightCapture.qml` — virtual-zero stable-weight capture (`rawWeight`/`cupWeight`/`minNet`/`maxNet` → `netWeight`/`onStableCaptured`), already used by bean dose capture.

This feature was prototyped in community PRs #1351/#1360, reworked in #1363 (virtual-zero capture, last-steam baseline, two scaling correctness fixes), and converged with review polish in #1365. The reference de1app skin **DSx2** computes `time = referenceSeconds × (scale − jug) / referenceMilk` — the same proportional model.

> This doc originally proposed a single *global* steam rate (D1). The shipped design is *per-pitcher*; the decisions below reflect what merged.

## Goals / Non-Goals

**Goals:**
- Steam auto-stop scales with the milk on the scale.
- Additive and reversible: one toggle fully disables it and reverts to fixed-duration steaming, keeping the calibration.
- Low daily friction: pour, place, wait for the ding, steam.
- One canonical scaling function (C++), reused everywhere.
- Robust to an un-zeroed scale, to the pitcher never leaving the scale, and to lifting the pitcher / switching screens.

**Non-Goals:**
- Steam-flow normalization of the rate (assumes a consistent steam flow; documented limitation).
- Modeling the fixed milk-aeration phase separately (linear scaling is the DSx2-proven approximation).
- BLE protocol changes (scaled value flows through `steamTimeout → TargetSteamLength`).
- Auto-calibrating from every pour (unsafe; calibration is one explicit tap).

## Decisions

### D1 — Per-pitcher calibration (changed from the original global-rate proposal)
Each pitcher preset stores its own `calibMilkG` paired with its `duration`. The empty-pitcher tare reuses `pitcherWeightG`.

*Why the change:* a global seconds-per-gram rate is conceptually cleaner (the rate is a machine/flow/temperature property, not the pitcher's), but per-pitcher calibration gives users a free way to keep **different references per drink or per person** — e.g. a hotter latte on a different pitcher pill — without any extra setting. The maintainer chose per-pitcher for that flexibility. Trade-off: re-teaching the rate per pitcher, and possible drift between pitchers.

### D2 — One toggle that gates ALL scaling, default OFF, auto-enabled on calibrate, preserving calibration
`SettingsBrew::milkAutoCaptureEnabled` (stored key unchanged; UI label "Weight-timed steaming") is checked **inside** `scaledSteamTime()`, which returns 0 when off. Every caller then falls back to the fixed duration, so the toggle disables auto-capture *and* pill-tap/steam-start scaling. Default is **off** — the feature is opt-in. Setting a pitcher's reference (`setSteamPitcherCalibration` with a positive value) flips it on automatically, so the user opts in by calibrating rather than hunting for a switch. The per-pitcher `calibMilkG` is independent of the toggle, so flipping it off and on never loses the calibration.

*Why:* the toggle must genuinely turn the feature off without discarding calibration, and default-off keeps it from silently scaling for users who haven't opted in. Putting the gate in the one helper means no per-call-site checks and no way for a path to bypass it; auto-enabling on calibrate keeps the daily flow a single tap.

### D3 — Scaling math centralized in C++
`SettingsBrew::netMilkForPitcher(index, scaleReading)` (net milk, requires a saved tare, bounds [50,1500]) and `SettingsBrew::scaledSteamTime(index, milkG)` (toggle gate + clamp [5,120], 0 = use fixed duration) are the single source of truth. QML helpers (`currentMeasuredMilk`, `scaledSteamTimeout`, `steamTimeForMilk`) are thin wrappers; the home-flow capture and pill-tap call the C++ directly.

*Why:* the prototypes had the formula in four QML copies. One C++ entry point removes the divergence and is the natural home for the toggle gate and the bounds.

### D4 — Reuse the virtual-zero `StableWeightCapture`; milk floor 50 g
Milk capture uses `rawWeight = scaleWeight`, `cupWeight = pitcherWeightG`, `minNet: 50`, `maxNet: 1500`. The 50 g floor (nobody steams less) also prevents a bean cup from tripping milk capture, closing the bean (5–45 g) / milk overlap.

### D5 — Apply at steam-start, not only on settle-capture
The virtual zero seeds to pitcher+milk when the loaded pitcher rests on the scale the whole time, so `onStableCaptured` never fires. So the steam page also tracks the last on-scale net milk and, at steam-start, applies the scaled time via `setSteamTimeoutImmediate` (effective mid-steam). `capturedMilkForScaling()` recovers the session milk (shared via the window) so the home→steam page change and lift-to-wand both keep the scaling.

### D6 — Mode/page gating prevents capture collisions
Bean capture is armed only outside steam mode; milk capture only in steam context. The IdlePage milk capture is additionally gated on `StackView.status === Active` so it can't fire alongside the steam page's copy when that page is pushed on top.

### D7 — Manual ±5 override latch
`steamTimeoutUserAdjusted` is set when the user taps ±5; while set, auto-capture and steam-start skip overwriting the hand-dialed time. Cleared on lift-off-scale, pitcher change, and session end. Event-based, no timer.

### D8 — Atomic last-pour + one-tap "Use as baseline"
`main.qml` watches the transition out of `Steaming` and writes `lastSteamMilkG`/`lastSteamTimeS` together only when a milk weight was captured. Steam setup shows `Last: Xg → Ys` and adopts it onto the selected pitcher in one tap (DSx2 learn-from-reality).

### D9 — Merged, relocated "Milk pitcher" row
The two redundant pitcher-weight controls were merged into one row (editable field + confirm-guarded Weigh + Tare/live/Clear + weight-timed hint), relocated under Steam Flow, above the weight-timed rows.

## Risks / Trade-offs

- **Per-pitcher rate can drift between pitchers** → accepted for the per-drink/per-person flexibility (D1).
- **Single rate per pitcher assumes consistent steam flow** → flow-normalization left as a future option; clamped to 5–120 s.
- **Linear scaling ignores the fixed aeration phase** → second-order; matches DSx2 and the user controls stretch by wand technique.
- **Requires a saved pitcher weight to scale** → low friction via the merged row's Weigh/edit; one consistent net-milk rule.

## Migration Plan

- Purely additive; no data migration. Existing users have no calibration → behavior identical until they calibrate. Settings key for the toggle is unchanged, so backup/restore round-trips.
- Rollback: toggle off (keeps calibration) or remove the calibration → fixed-duration steaming, no residual effect.

## Open Questions

- Whether to offer steam-flow normalization later (one calibration applied across flows).
- Whether the very first pour should auto-adopt as the reference for zero-setup first use (currently explicit, for safety).
