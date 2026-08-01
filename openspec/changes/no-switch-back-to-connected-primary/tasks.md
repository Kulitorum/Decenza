# Tasks

## 1. Gate the switch-back

- [x] 1.1 Add `BLEManager::shouldRequestSwitchBack(bool scaleConnected, bool directAttemptFailed)` beside the other reconnect predicates in `src/ble/blemanager.h`, documenting why `m_wifiDirectAttemptFailed` is the backup discriminator and citing the observed outage.
- [x] 1.2 Apply it in `maybeAutoConnectBrowsedScale()` (`src/ble/blemanager.cpp`): when a scale is connected and the predicate is false, return without emitting `wifiPrimaryReachable` and without falling through to `scaleDiscovered`.
- [x] 1.3 Drop "Reconnect" from the switch-back log line — the same function serves the user scan's browse and probe.
- [x] 1.4 Correct the comment in `onScaleConnectedChanged()` that asserted an already-connected scale always routes through `switchToWifiPrimary()`.

## 2. Tests

- [x] 2.1 Add `switchBackOnlyWhenTheConnectedScaleIsABackup()` to `tests/tst_wifiscalediscovery.cpp` covering all four (connected, directAttemptFailed) combinations.
- [x] 2.2 Verify the test can fail: revert 1.2 and confirm the `(true, false)` assertion goes red.
- [x] 2.3 Run the full suite via the Qt Creator MCP (`run_tests`, scope `all`) before opening the PR.

## 3. Ship

- [ ] 3.1 Open the PR against `main` and review it.
- [ ] 3.2 `openspec archive no-switch-back-to-connected-primary` as the last commit on the branch.

## Notes

No manual entry in the wiki manual: the change removes an unintended disconnect,
it does not add or alter a documented user-visible feature.
