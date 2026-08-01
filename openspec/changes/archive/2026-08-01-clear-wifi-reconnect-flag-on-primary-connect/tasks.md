# Tasks

## 1. Fix the flag lifecycle

- [x] 1.1 Add `BLEManager::connectedScaleIsWifiPrimary()`, using the same type test `main.cpp:2465` applies, and say in its comment why the two must agree — including the limit it inherits, which is reachable rather than theoretical.
- [x] 1.2 Add the pure predicate `connectClearsDirectAttemptFailed(wasWifiFallbackConnect, connectedScaleIsWifiPrimary)` beside the two existing ones.
- [x] 1.3 Apply it in `onScaleConnectedChanged()`, replacing the bare `!wasWifiFallbackConnect` test, and record on the spot why the fallback flag is the wrong question.

## 2. Stop the false log line

- [x] 2.1 Return early from `maybeAutoConnectBrowsedScale()` when `connectedScaleIsWifiPrimary()`, before either the switch-back branch or the plain emit.
- [x] 2.2 Narrow the "almost always the WiFi->BLE fallback" premise in the comment below it. The early return excludes a connected WiFi scale, not every other way a BLE scale could be connected, so it stays a hedge. Drop the unsourced duration it carried — the same defect already deleted from the sibling comment in `onScaleConnectedChanged()`.

## 3. Stop a browsed address outliving its request

- [x] 3.1 Clear `m_browsedPrimaryIp` at the start of `probeWifiPrimaryReachable()`.
- [x] 3.2 Correct `switchToWifiPrimary()`'s "Consumed either way" comment, which was false for a declined request.

## 4. Tests

- [x] 4.1 Add `primaryConnectClearsTheFlagEvenDuringAFallback()` to `tests/tst_wifiscalediscovery.cpp` — four rows, including the one that was wrong.
- [x] 4.2 Verify it can fail: restore the old rule and confirm it goes red. Done — 109 passed, 1 failed.
- [x] 4.3 Full suite via the Qt Creator MCP. Done — 110 passed, 0 failed, no warnings.

## 5. Centralize the scale-type literal

- [x] 5.1 Replace the hand-written `QStringLiteral("decent-wifi")` with `ScaleTypeIds::scaleTypeId(ScaleType::DecentScaleWifi)` at all 6 sites in `blemanager.cpp` and all 4 in `main.cpp`. Introducing the central form for one new call while leaving ten copies alongside it is the drift the rule exists to stop, and it is what makes the "the two layers cannot disagree" claim true rather than aspirational.

## 6. Ship

- [x] 6.1 Open the PR and review it.
- [ ] 6.2 `openspec archive clear-wifi-reconnect-flag-on-primary-connect` as the last commit on the branch.

## Notes

No wiki manual entry: nothing user-visible changes except the removal of a log
line that described an event which was not happening.

The behaviour this fixes is only reachable on Android in practice, since the
browse-driven recovery path is the one the direct mDNS query's failure mode
produces. It cannot be verified locally; per project policy it rides to the next
beta rather than a sideload.
