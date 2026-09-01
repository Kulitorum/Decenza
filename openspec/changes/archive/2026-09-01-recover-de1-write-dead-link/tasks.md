## 1. Recover a silent link

- [x] 1.1 Evaluate `m_notificationLiveness` on activity that already occurs — including the write-failure path — instead of only inside `connectToDevice()`; verify by test that a link going silent is observed with nothing calling connect
- [x] 1.2 Replace `NOTIFICATION_STALE_MS` with a threshold below the platform's own disconnect notice (22.5 s in both logged episodes), commented as provisional pending the on-device cadence measurement in `harden-de1-ble-reliability` 5.2 / 8.5; verify the comment states the bound and why this log cannot supply the number
- [x] 1.3 On a confirmed stale link, tear down and emit `disconnected()` so `main.cpp`'s existing ladder reconnects; verify by test that exactly one teardown and one `disconnected()` occur and no direct connect is issued
- [x] 1.4 Defer the teardown while the machine is in an active phase AND writes are still succeeding, using the write outcomes `BleGattQueue` already reports; verify by test that a silent-but-writing link is left alone mid-shot and a silent-and-not-writing one is torn down
- [x] 1.5 Do not act when the controller has already reported the link disconnected, respecting `m_disconnectedEmittedForAttempt`; verify by test that a platform disconnect arriving first produces one reconnect, not two

## 2. Stop misdirecting the user

- [x] 2.1 Rewrite the dead-link warning to say what the system is doing instead of telling the user to reconnect from the Connections page or over MCP; verify `scripts/check_log_markers.py` passes and read the emitted line
- [x] 2.2 Log the teardown and its outcome — link back and delivering, or not — at a tier the connection views show by default, never recording a failed attempt as a recovery; verify by test on both outcomes
- [x] 2.3 ~~Withhold a scheduled profile-upload retry while writes are failing~~ — reverted. The hold caused a defect in each of three rounds (stranded upload, then a 1 Hz poll, then an unbounded wait that pinned the "Reconnecting…" toast and blocked the escalating dialog that previously appeared in ~15 s). Its only benefit was suppressing a wrong "power-cycle the DE1" message on a link fault; the bounded existing behaviour is better than an unbounded wait. The dead-link WARN now names the manual remedy for the case automatic recovery does not cover

## 3. Tests and docs

- [x] 3.1 ~~Extend the mock transport to present a link that reports connected while delivering no inbound data, and to fail writes on demand~~ — not needed. `isConnected()` requires a real `QLowEnergyController`, so the teardown path is unreachable from a unit test whatever the mock does; the decision was split into the pure `shouldRecoverAfterAbandonedWrite()` instead and the tests drive that. Same reason `BlePriorityDetector` is pure and clock-free
- [x] 3.2 Add the regression case from the submitted log: silent link, teardown, reconnect, data flowing again; verify it fails against pre-change behaviour before it passes
- [x] 3.3 Add test slots to existing `tst_bletransport*` files rather than new `tst_*.cpp` files, per `docs/CLAUDE_MD/TESTING.md`; verify no new test source file is added
- [x] 3.4 Document the recovery in `docs/CLAUDE_MD/BLE_PROTOCOL.md` beside the retry-budget section; verify it states the trigger, the guard, and that the reconnect is the existing ladder

## 4. Verification and review

- [x] 4.1 Run the full test suite through the Qt Creator MCP (asking first, since it is shared) and verify it is green with no new warnings
- [x] 4.2 Open a PR, run `/pr-review-toolkit:review-pr`, address findings before merge
- [x] 4.3 Archive the change with spec sync as the final commit on the same PR
