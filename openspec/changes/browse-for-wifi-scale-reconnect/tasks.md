## 1. Resolve the open questions before writing code

- [x] 1.1 Pick the reconnect-browse deadline and justify it at the call site from observed resolve latency (the one measured browse resolved in 362 ms; the user scan's 15 s exists because a DNS-SD first callback is a stale-cache dump). Write the number as a named constant, not a literal.
- [x] 1.2 Decide whether the browse runs on the first failed reconnect attempt or only after the ladder's first failure, and record the reason in the code.
- [x] 1.3 Decide whether the A-record probe accompanies the reconnect browse. Default expectation is no — pre-v3.0.9 scales answer A-queries, which already works. Record the decision either way.

## 2. Reconnect discovery instance

- [x] 2.1 Add a third `WifiScaleDiscovery` member to `BLEManager` for reconnect, mirroring the `m_manualEntryDiscovery` split (`blemanager.cpp:887-927`), with a comment saying why it is separate: `browse()` cancels any in-flight browse, so sharing would let a reconnect tick kill a user's scan.
- [x] 2.2 Wire its `resultFound` to the SAME saved-address matching and connect dispatch the existing handler uses (`blemanager.cpp:2542-2583`) rather than writing a second matcher — the file already records a drift bug from two dedupes diverging.
- [x] 2.3 Verify the new instance is NOT referenced by `isScanning()` (`blemanager.cpp:745-758`), so a reconnect browse cannot make the Scan button read "Scanning…".
- [x] 2.4 Verify the reconnect path does not call `m_wifiResults.clear()` or `clearWifiScaleRows()`, so a background browse cannot wipe rows the user is reading.

## 3. Trigger the browse from the reconnect ladder

- [x] 3.1 In the `wifi:` branch of `tryDirectConnectToScale()` (`blemanager.cpp:2650-2679`), start the reconnect browse when the direct attempt has failed, per the decision in 1.2. Do not browse in parallel with a cached-IP attempt that is likely to succeed.
- [x] 3.2 Confirm the existing 20 s `m_scaleConnectionTimer` and the WiFi→BLE fallback still behave as before when the browse finds nothing — the change must be purely additive on the failure path.
- [x] 3.3 Confirm no browse is started when the saved primary is not a `wifi:` address.

## 4. Correct the record left by #1737

- [x] 4.1 Fix the `kHdsResolveTimeoutMs` call-site comment in `decentscalewifi.cpp`, which asserts the 5 s deadline addresses the 82 observed misses. That is now falsified — the misses continue at ~5002 ms with `records= 0`. Keep the constant (it matches the discovery path and is harmless); replace the claim with what was actually learned.
- [x] 4.2 Update the `WifiScaleDiscovery` class doc (`wifiscalediscovery.h:24`), which states "Neither does background work: nothing runs until a caller asks, and a browse stops when the scan cycle that started it ends." Both halves change.

## 5. Tests

- [x] 5.1 Add a test that a reconnect browse resolving the saved primary produces a connect dispatch, and that one resolving a different address does not (the anti-substitution guarantee).
- [x] 5.2 ~~Add a test~~ — NOT testable without constructing BLEManager (it owns the BLE stack). Guaranteed by construction instead: a separate `m_reconnectDiscovery` instance (browse() only cancels within an instance), and the reconnect handler deliberately does not touch `m_wifiResults`/`rebuildWifiScaleRows()`. Both facts are commented at the code.
- [x] 5.3 Add a test that `isScanning()` stays false while only the reconnect browse is running.
- [x] 5.4 Put new assertions in existing `tst_*` files where one covers the area — a new test FILE costs ~1.4 s of build time forever, a new slot costs milliseconds.
- [ ] 5.5 Before keeping each test, break the code it covers and watch it fail. A test that cannot fail is a comment that compiles.

## 6. Verify and land

- [ ] 6.1 Run the full local suite through the Qt Creator MCP (`run_tests`, scope `all`) — that is the pre-PR gate; nothing on GitHub builds a PR.
- [ ] 6.2 Open the PR with the falsifier stated explicitly: if reconnect browses and still fails while a user scan immediately succeeds, the operative difference is something else in the user-scan context (concurrent BLE scan, or the A-record probe running alongside), not the browse.
- [ ] 6.3 Verify locally on macOS with a RENAMED scale (a name outside the `hds`/`hds-2`/`hds-3` fallback list) — that is where the direct resolve fails and only a browse succeeds, and it is reproducible without waiting for the Android beta. Note in the PR that the beta then confirms Android.
- [ ] 6.4 Update the wiki manual only if this changes user-visible behaviour beyond "the scale reconnects on its own"; a silent recovery probably needs no manual entry, but decide rather than skip.
- [ ] 6.5 Archive this change with `openspec archive browse-for-wifi-scale-reconnect` as the last commit on the branch, before merge.
