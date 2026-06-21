## 1. C++ scaling core (SettingsBrew)

- [x] 1.1 `milkAutoCaptureEnabled` toggle (default **off**; UI label "Weight-timed steaming"; auto-enabled when a pitcher reference is set); persisted `lastSteamMilkG`/`lastSteamTimeS`; `setSteamPitcherCalibration()` for per-pitcher `calibMilkG`. All on `SettingsBrew`, not the `Settings` façade.
- [x] 1.2 `netMilkForPitcher(index, scaleReading)` — net milk = scale − saved tare; requires a saved pitcher weight; bounds [50,1500]; 0 otherwise.
- [x] 1.3 `scaledSteamTime(index, milkG)` — single source of truth: returns 0 when the toggle is off, the preset is missing/disabled/uncalibrated, or duration/milk ≤ 0; otherwise `clamp(round(duration × milk / calib), 5, 120)`.
- [x] 1.4 Serializer round-trips `pitcherWeightG`/`calibMilkG`/`disabled` and `milkAutoCaptureEnabled`; `selectedPitcher` applied after the preset rebuild.

## 2. Steam page

- [x] 2.1 Thin QML wrappers over the C++ helpers (`currentMeasuredMilk`, `scaledSteamTimeout`, `steamTimeForMilk`); pill-tap / live-click use them.
- [x] 2.2 `syncSteamTimeout()` preserves a scaled value across lift-to-wand; recovers session milk via `capturedMilkForScaling()`.
- [x] 2.3 Apply scaled time at steam-start via `setSteamTimeoutImmediate` (covers the pitcher-never-leaves-scale case); track `lastOnScaleMilk`.
- [x] 2.4 Milk `StableWeightCapture` (virtual-zero, `minNet: 50`); ding + banner + "wait for the bell" hint.
- [x] 2.5 Reference-milk row, "Use as baseline" (`Last: Xg → Ys`), live "Expected steam time" row, and the "Weight-timed steaming" toggle.
- [x] 2.6 Merged "Milk pitcher" row (editable + confirm-guarded Weigh + Tare/live/Clear), relocated under Steam Flow.
- [x] 2.7 Manual ±5 override latch (`steamTimeoutUserAdjusted`), honored by capture and steam-start, cleared on lift / pitcher change / session end.

## 3. Home-screen steam flow (IdlePage)

- [x] 3.1 Milk capture gated on steam mode + `StackView.status === Active` (no double-fire with the steam page); `minNet: 50`.
- [x] 3.2 Capture + pill-tap scale via the C++ helper; bean capture stays gated to non-steam modes.

## 4. Elapsed-time capture (main.qml)

- [x] 4.1 Watch the transition out of `Steaming`; write `lastSteamMilkG`/`lastSteamTimeS` atomically only when a milk weight was captured this session.

## 5. Verification

- [x] 5.1 Builds clean on macOS via Qt Creator incl. the full test suite (0 errors, 0 warnings).
- [x] 5.2 Reviewed (general / silent-failure / comment-accuracy passes); findings fixed: ±5 latch stuck-on, duration guard, tab-to-hidden-element, purge a11y, stale comments.

## 6. Follow-ups (not done — optional)

- [ ] 6.1 Optional steam-flow normalization of the calibration (one reference applied across flows).
- [ ] 6.2 Slider writes use `onValueModified` (per-step QSettings churn); consider `onValueCommitted`.
- [ ] 6.3 Diagnostic `qWarning` on the "shouldn't happen" guard paths (null `Window.window`, serializer no-op import, negative clamps).
