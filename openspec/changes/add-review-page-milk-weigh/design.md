## Context

Milk capture is a `StableWeightCapture` (virtual-zero) instance on each page that captures: `idleMilkCapture` (IdlePage.qml:618) and `milkCapture` (SteamPage.qml:2247). Each has its own `onStableCaptured` handler that writes `AppShell.sessionMeasuredMilkG`, computes `Settings.brew.scaledSteamTime()`, writes `steamTimeout`, calls `MainController.applySteamSettings()`, and plays the ding. The two copies have already drifted: SteamPage respects a ±5 override and sets `steamTimeoutScaled`, IdlePage does not.

After a shot the scale's zero is the cup, because espresso tares at cycle start. So on the review page "cup on" reads about the yield, and "cup lifted" reads about minus the cup's weight.

A group-head steam pushes `SteamPage` over the review page (`main.qml` `showOperationPage`). Its `StackView.onActivated` calls `MainController.selectSteamPitcher(selected, steamPage.lastOnScaleMilk)`. That call reads net milk from the live scale first and uses the fallback only when the scale shows none. After a shot the cup, not the pitcher, is on the scale, so the live reading gives nothing. The fallback `lastOnScaleMilk` is 0 on a fresh page, so the base duration is sent to the DE1. That overwrites the scaled time an earlier capture had already sent. The captured milk is re-applied only at steam start, through `capturedMilkForScaling()`.

This is the leading explanation for the `keep-milk-weight-across-shot` report (milk captured on idle, then group-head shot, review page, group-head steam, giving the fixed duration). On the idle path that works, the pitcher is usually still on the scale when Steam is pressed, so the live read succeeds. The #1711 steam-start logging records the milk sources and the applied value, so one reproduction confirms or refutes this.

## Goals / Non-Goals

**Goals:**
- One capture path for three pages: one apply helper, one detector component.
- A capture that cannot adopt the cup as the empty scale.

**Non-Goals:**
- Auto-capture on the review page without a tap. The detector adopts its first settled reading as zero, and on this page that reading is the cup.
- Auto-detecting the pitcher from its weight.
- Any change to the scaling math, the 50–1500 g capture bounds or the capture sound setting.

## Decisions

**1. A capture attempt never tares — on idle, in the compact steam widget, or from the button.** *(Revised twice after on-device testing.)*
- `MilkCapture.startAttempt()` only drops stale session milk; idle's Steam selection and the review button both call it. The capture measures from the empty reading it sees settle, so a cup or pitcher left on at the start becomes the first zero, and lifting it re-adopts the lower settled reading as empty.
- *Rejected (first):* detect the cup by an absolute reading above 3 g and hold "Lift cup". It trusted the scale's own zero; an empty scale read 6.6 g and the button never left "Lift cup".
- *Rejected (second):* tare at the start of an attempt, as idle's Steam selection and the compact steam popup used to. The capture never needed it (it measures from the settled empty reading), and the log showed the HDS over WiFi (fw 3.1.14) sending garbage frames of roughly ±1,000 g around each tare, plus a post-tare reading that sat at 12.6 g with the pitcher on and drifted. An early hypothesis that the tare shifted the scale's zero by ~23 g was not borne out: that test scale turned out to be miscalibrated, and the low readings matched its own display. Removing the tare keeps idle and the button one function. User-initiated tares (weight widget, Steam page Tare button) are unchanged.

**2. One capture component, `MilkCapture.qml`, used by all three pages.** *(Revised: replaces an `AppShell.applyCapturedMilk` helper.)*
- It fixes the scale input, the pitcher-weight subtraction, the 50–1500 g bounds and settle, the reset on tare, and the apply step (session milk, scaled time, send to the DE1, ding). Each page sets only `active`, plus `lockTime` on the steam page (`!steamTimeoutUserAdjusted`).
- It emits `milkCaptured(milk, seconds)`; each page keeps only its own presentation (idle banner, steam banner plus `steamTimeoutScaled`, the review-page label).
- *Rejected:* a shared apply function with three `StableWeightCapture` instances configured per page. That still left the configuration in triplicate, and the review page's copy had already started to drift.

**3. Long-press calls `MainController.selectSteamPitcher(next, 0)`.**
- This is the call every pill uses, so recipe deactivation, the heater policy and the `sessionMeasuredMilkG` reset all come along unchanged.
- The milk fallback is 0, because the selection clears the captured weight anyway.
- The next usable index is computed in the page from `Settings.brew.steamPitcherPresets`, skipping `isHeaterOffPitcher`, `disabled`, and `pitcherWeightG <= 0`.

**4. SteamPage activation passes `capturedMilkForScaling()` instead of `lastOnScaleMilk`.**
- That function already prefers `sessionMeasuredMilkG` and falls back to `lastOnScaleMilk`. Activation then sends the scaled time instead of the base duration.
- This changes nothing when no milk was captured.

**5. Fold in `keep-milk-weight-across-shot` instead of carrying it separately.**
- Its diagnostics are merged and its remaining tasks are a reproduction, a fix and a spec correction, all of which land here. Two open changes would describe one steam-start path from two angles.
- It is archived first, as-is, so its diagnosability requirement is promoted unchanged. This change's archive then applies on top.
- Its unverified tasks are reconciled against this change's end-to-end check, or recorded as held for the beta if that check cannot run before merge.

**6. Correct the calibration requirements through this delta.**
- The main spec's "Per-pitcher calibration" and the requirements built on it describe `calibMilkG`/`duration` ratios. `SettingsBrew::scaledSteamTime` has used one global `steamSecondsPerGram` since the steam-rate migration (`settings_brew.cpp:854-865`).
- The delta removes the stale requirement, adds "One global steam rate", and rewrites the four dependent requirements.
- The spec's Purpose line also says "per-pitcher reference calibration". A delta cannot change a Purpose, so it is left as-is and flagged rather than hand-edited.

**7. Button pattern.**
- Same `Rectangle` + `AccessibleMouseArea` shape as `readTdsButton`, with `supportLongPress: true`.
- Its width grows with the label from the R2 width (80) up to a cap, and the label is cut short with "…" past the cap.
- The accessible name is "Weigh milk, <pitcher>" (plus the weight once captured). Next pitcher is exposed as an `Accessible.increaseAction` handler, the established Qt action a screen reader can invoke without a long-press.

## Risks / Trade-offs

- **Pitcher set down less than ~1 s after the cup is lifted** → the detector seeds on pitcher+milk and does not capture.
  - Mitigation: "Place pitcher" only appears once seeded, so the label tells the user when to put it down. Lifting and replacing recovers, same as idle.
- **Scale tared manually with the cup on** → the cup reads 0, so the load check passes it as empty and the detector seeds on the cup.
  - Mitigation: lifting the cup drops the reading, and the detector re-adopts the lower settled reading as zero (the existing empty branch). The capture is correct as long as the scale is empty for about 1 s before the pitcher goes down.
- **A mid-pour pause of ~2.5 s captures a partial weight** → same behaviour as idle today. Accepted.
- **Cycling deactivates an active recipe** → intended; it matches pitcher selection everywhere else.
