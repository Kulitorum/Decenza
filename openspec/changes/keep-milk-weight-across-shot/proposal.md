## Why

A user captures milk weight on the idle screen, brews an espresso shot via the DE1's own GHC buttons (no app interaction), lands on the Shot Review page, then presses the GHC's physical Steam button — and weight-timed steaming does not scale to the milk they captured; it falls back to the recipe's fixed duration.

Investigation so far (two rounds, both purely static) ruled out the two most obvious theories: `AppShell.sessionMeasuredMilkG` is not cleared during an espresso shot or by anything in `PostShotReviewPage.qml`, and the machine-driven navigation into `SteamPage` (`main.qml:600-625`, `main.qml:3438-3442`) reads it through the same code path (`SteamPage.qml`'s `onIsSteamingChanged`/`StackView.onActivated`) regardless of which page the machine-driven `pageStack.replace` came from. The user confirmed they use a saved Drink Recipe with its own steam/pitcher block, and that nothing else (recipe or pitcher reselection) happens between capturing the milk and pressing GHC Steam — which also rules out the recipe-reactivation-reassigns-pitcher theory (`maincontroller.cpp:1663`, `brew->setSelectedSteamCup()`), since that only resets the capture when the pitcher index actually changes.

The remaining, unconfirmed candidate: weight-timed steaming's per-pitcher **calibration** (`calibMilkG` + reference `duration`) may be missing for the recipe's pitcher ("Small," per the user's live settings) — the spec's own "Uncalibrated pitcher" fallback would then silently use the recipe's fixed duration no matter how correctly the milk weight was captured and carried through. This can't be confirmed from static code or from the currently-exposed MCP settings surface (no `calibMilkG` field is exposed by `steam_pitcher_list`), so rather than ship a fix for a guessed cause, this change adds targeted diagnostic logging at the exact scaling decision point, so the next real occurrence pins the cause definitively before any behavioral fix is written.

## What Changes

- Add debug logging at the point weight-timed steaming decides between a scaled duration and the fixed fallback (`SteamPage.qml`'s `onIsSteamingChanged` and `syncSteamTimeout()`), recording: the session-captured milk weight, the selected pitcher's name and calibration (`calibMilkG`/reference `duration`, or "uncalibrated"), the computed scaled duration (or 0), and which value was actually applied and why.
- No behavior change. This is diagnostics-only — the actual fix (once the log identifies the real cause) is out of scope here and will be proposed separately.
- Follow `docs/CLAUDE_MD/LOGGING.md`'s scope note: this is QML UI/session logic, not a device/radio/transport subsystem, so it uses plain `console.log` consistent with `SteamPage.qml`'s existing debug lines (`SteamPage.qml:119-132`) rather than the C++ marker-macro system, which only gates device/radio subsystems.

## Capabilities

### Modified Capabilities
- `weight-timed-steaming`: adds a diagnosability requirement — when the system falls back to the fixed duration instead of scaling, it SHALL be possible to determine why from the debug log (captured milk present but no calibration, vs. no milk captured, vs. scaling disabled). No scaling behavior itself changes.

## Impact

- `qml/pages/SteamPage.qml` — `onIsSteamingChanged`, `syncSteamTimeout()`, `capturedMilkForScaling()`: add decision-point logging only.
- No BLE, database, settings-schema, or behavior changes.
