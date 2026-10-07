## Why

A common routine is to pull a shot, land on the post-shot review page, and start steaming milk from there. Weight-timed steaming only captures the milk weight on the idle screen and the steam page, so from the review page there is no easy way to capture it, and a group-head-started steam falls back to the fixed duration. Issue [#1662](https://github.com/Kulitorum/Decenza/issues/1662) shows the related confusion: users try to tare after placing the pitcher, which is the opposite of how capture works.

## What Changes

- Add a **milk-weigh button** to the post-shot review page header, immediately left of the Read TDS (R2) button. Its resting label is the selected pitcher's name.
- **Tap** runs a guided capture. The button asks you to lift the cup if one is still on the scale, waits for an empty scale, then asks you to place the pitcher. It captures the net milk once the reading settles. Milk can be poured before the pitcher goes down or poured into it on the scale. The result shows on the button as `<pitcher> · <milk> g`. Tapping again redoes the capture.
- **Long-press** cycles through the usable pitchers: Heater off, disabled presets and presets with no saved pitcher weight are skipped. Cycling is an ordinary pitcher selection, with the same effects as selecting a pitcher anywhere else (an active recipe deactivates, a captured milk weight clears). Screen-reader users get the same cycle as an accessibility action.
- The button shows only when weight-timed steaming is on, at least one usable pitcher exists, and a real (non-flow) scale is connected.
- A capture feeds the same session milk weight the idle and steam captures use, so a steam started from the group head scales to it.
- **Fix:** opening the steam page before steaming starts no longer resets the steam time to the pitcher's base duration when milk was already captured this session. Today the scaled value only comes back at steam start, so the page shows the wrong time until then.
- **Refactor:** the "apply a captured milk weight" logic, now duplicated in `IdlePage.qml` and `SteamPage.qml`, moves into one shared helper used by all three pages.
- **Spec correction:** the `weight-timed-steaming` spec still describes per-pitcher reference calibration (`calibMilkG` + `duration`). The code has used one global seconds-per-gram rate since the steam-rate migration. The calibration requirements are rewritten to match the code; no behaviour changes.
- **Absorbs `keep-milk-weight-across-shot`:**
  - That change shipped steam-start diagnostics (#1711) for a milk weight captured on idle not scaling a group-head steam started from the review page.
  - Its remaining work is folded in here. Its reproduction becomes this change's end-to-end check, the Steam page activation fix above is the candidate fix for its report, and its spec follow-up is the correction above.
  - Its likely cause: opening the Steam page finds no milk on the live scale (the espresso cup is on it), so it sends the base duration over the scaled time the idle capture had already sent.
  - It is archived in the same PR.
- **Manual:** one sentence in the wiki's Weight-timed steaming section about the review-page button. The section's formula and calibration rows also get corrected: they still describe the per-pitcher reference and "Use as baseline".

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `weight-timed-steaming`:
  - Adds requirements for capturing milk from the post-shot review page and for cycling pitchers from its button.
  - Widens "scaled time survives the page change" to milk captured this session on any screen, including when the steam page opens before steaming starts.
  - Replaces "Per-pitcher calibration" with "One global steam rate", and rewrites the scaling, toggle, uncalibrated and calibrate-from-pour requirements to the global rate.

## Impact

- `qml/pages/PostShotReviewPage.qml`: the new button, its own `StableWeightCapture` instance, and the cycle and accessibility handling.
- `qml/pages/IdlePage.qml`, `qml/pages/SteamPage.qml`: capture handlers call the shared helper.
- `qml/AppShell.qml`: hosts the shared helper next to `sessionMeasuredMilkG`.
- `qml/components/StableWeightCapture.qml`: exposes whether an empty-scale baseline has been established.
- `qml/pages/SteamPage.qml` `StackView.onActivated`: passes the session-captured milk as the fallback.
- No C++, BLE, database or settings-schema changes. No new settings.
- `openspec/changes/keep-milk-weight-across-shot` is reconciled and archived in this PR (see tasks).
- Wiki `Manual.md`, Weight-timed steaming section.
