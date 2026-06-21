## Why

A fixed steam timeout over-heats a half-full pitcher and under-heats a full one: the DE1 stops steaming purely by time (`TargetSteamLength`), and time-to-temperature is proportional to milk mass. Users who steam varying amounts of milk must constantly re-adjust the timer by hand. Scaling the steam time to the milk actually on the scale makes the auto-stop land at the right temperature every time, regardless of how much milk is in the pitcher.

The capability exists in the de1app world (the DSx2 skin, by Damian-AU) and was prototyped/shipped across community PRs ([#1351](https://github.com/Kulitorum/Decenza/pull/1351), [#1360](https://github.com/Kulitorum/Decenza/pull/1360), [#1363](https://github.com/Kulitorum/Decenza/pull/1363)). This document records the **as-built** design that merged in [#1365](https://github.com/Kulitorum/Decenza/pull/1365).

> **Status: implemented.** This doc was originally written proposing a single *global* steam-rate calibration; the shipped design uses a *per-pitcher* calibration instead (see Design D1). The text below reflects what shipped.

## What Changes

- **Weight-timed steam auto-stop.** When the milk pitcher rests on a connected scale, the steam time is set to `clamp(round(duration × measuredMilk / referenceMilk), 5, 120) s` and the DE1 auto-stops at that time. `measuredMilk = scaleReading − pitcherTare`.
- **Per-pitcher calibration.** Each steam pitcher preset carries its own `calibMilkG` (reference milk weight) paired with its `duration`. Different pitchers (e.g. different drinks / different people's preferred temperature) keep independent references.
- **Reuse the existing per-pitcher tare.** The empty-pitcher weight already stored per preset (`pitcherWeightG`) is the milk tare. Net milk requires a saved pitcher weight — one consistent rule, shared with the auto-capture gate.
- **Calibrate from a real pour.** The last steam session's actual elapsed time + measured milk are saved atomically at session end; a one-tap **"Use as baseline"** adopts them as the selected pitcher's reference.
- **Automatic milk capture.** The shared virtual-zero `StableWeightCapture` detects the settled milk weight (robust to an un-zeroed scale), dings, and locks the scaled time. Applied at steam-start too, for the case where the loaded pitcher never leaves the scale and the settle detector can't fire.
- **One on/off toggle that preserves calibration.** A single switch ("Weight-timed steaming", **default off**) gates **all** scaling — when off, every path uses the preset's fixed duration. Setting a pitcher's reference milk turns it on automatically (the explicit opt-in), and the per-pitcher calibration is retained, so toggling off then on resumes immediately.
- **Single scaling helper in C++.** `SettingsBrew::scaledSteamTime()` and `netMilkForPitcher()` are the one source of truth; QML callers (steam page + home flow) wrap them — no duplicated formula.
- **Manual ±5 survives auto-capture.** A nudge of the steam timer is preserved against re-scaling for the rest of the session.

## Capabilities

### New Capabilities
- `weight-timed-steaming`: Scaling the DE1 steam auto-stop time to the measured milk weight — per-pitcher calibration, calibrate-from-a-real-pour, virtual-zero milk auto-capture, the on/off toggle, manual-override preservation, and the fixed-duration fallback when uncalibrated/off.

### Modified Capabilities
<!-- None. Existing fixed-duration steaming is preserved as the off/uncalibrated fallback. -->

## Impact

- **`src/core/settings_brew.{h,cpp}`** — `milkAutoCaptureEnabled` toggle; persisted `lastSteamMilkG`/`lastSteamTimeS`; `setSteamPitcherCalibration()`; and the two scaling helpers `netMilkForPitcher()` / `scaledSteamTime()`. All on `SettingsBrew`, not the `Settings` façade.
- **`qml/pages/SteamPage.qml`** — milk capture, merged "Milk pitcher" row (under Steam Flow), Reference-milk + "Use as baseline" + expected-time rows, the toggle, steam-start apply, `syncSteamTimeout()`, ±5 latch.
- **`qml/pages/IdlePage.qml`** — milk capture for the home-screen steam flow (gated to the active page), pill-tap scaling.
- **`qml/main.qml`** — records the actual elapsed steam time + measured milk at session end (atomic pair), regardless of which screen steamed.
- **`src/core/settingsserializer.cpp`** — round-trips `pitcherWeightG`/`calibMilkG`/`disabled` and `milkAutoCaptureEnabled`.
- No BLE protocol changes; the scaled value flows through the existing `steamTimeout → TargetSteamLength` path.
