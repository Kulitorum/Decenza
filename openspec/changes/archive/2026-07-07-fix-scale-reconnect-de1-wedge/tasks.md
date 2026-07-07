## 1. Timeout aborts the controller (load-bearing fix)

- [x] 1.1 In `BLEManager::onScaleConnectionTimeout()`, when the scale is not connected, actually tear down the in-flight scale controller via `m_scaleDevice->disconnectFromScale()` (QtScaleBleTransport deletes the controller), not just clear `m_directConnectInProgress`.
- [x] 1.2 Verified teardown is safe: `QtScaleBleTransport::disconnectFromDevice()` disconnects the controller's signals before tearing down, so a connecting→disconnected abort fires no spurious `disconnected()`/`scaleDisconnected()` cascade and doesn't flip `connectedChanged`.
- [ ] 1.3 Verify via log on-device that no scale controller remains in `Connecting` after the timeout (build/test).

## 2. Background scale reconnect → scan-then-connect

- [x] 2.1 Change the periodic scale reconnect tick so that, with no foreground trigger active, it starts/continues a passive scan and does NOT open a direct `connectToDevice()` to the saved address. (Added `allowDirectConnect` param to `tryDirectConnectToScale`; background ladder in `main.cpp` passes `false` → scan-only branch.)
- [x] 2.2 Route the saved-scale connect exclusively through the scan-discovery path ("found saved scale in scan, using scanned device") for the background case. (Existing `onDeviceDiscovered` "saved BLE primary → always auto-connect" handles the connect when the scale is seen advertising.)
- [x] 2.3 Confirm the scale reconnect no longer logs `Direct wake - connecting` on the background timer when the scale is absent. (Scan-only branch skips the `emit scaleDiscovered(deviceInfo)` direct-connect; logs "scan only" instead.)

## 3. Bounded foreground direct-connect fast-path

- [x] 3.1 Added `abortScaleDirectConnectIfPending()` + a `kScaleDirectConnectAbortMs` (4 s) `QTimer::singleShot` armed when a foreground direct-connect starts; on the deadline it tears down the parked controller and keeps scanning.
- [x] 3.2 Foreground triggers retain the direct fast-path (default `allowDirectConnect=true` from the switch/startup/DE1-wake callers); only the background ladder passes `false`.
- [x] 3.3 Background reconnect uses the scan-only branch (returns before the direct path), so it never arms the fast-path.
- [x] 3.4 Already staggered: the existing `m_de1DirectConnectInFlight` defer block holds the scale direct-connect until the DE1 settles (15 s cap).

## 4. Prevent double-connect races

- [x] 4.1 Single physical scale / transport: `main.cpp`'s `scaleDiscovered` handler reuses the one `physicalScale` and calls `connectToDevice()` on its transport, which replaces (cleans up) the prior controller — there is never a second concurrent controller for the same scale.
- [x] 4.2 A scan discovery during an in-flight direct-connect is the intended "upgrade to the real advertising device" path (clears `m_directConnectInProgress` at `onDeviceDiscovered`), not a second independent connect.

## 5. DE1 direct-connect bounded abort + connect-hang recovery

- [x] 5.1/5.2 Added a connect watchdog in `BleTransport` (`m_connectWatchdogTimer`, `CONNECT_WATCHDOG_MS=35000`): armed on `ConnectingState`, stopped on any resolution and in `disconnect()`. If it fires while still Connecting it aborts the hung controller and synthesizes `disconnected()`, so the next reconnect recreates the controller (the watchdog is the DE1's bounded abort).
- [x] 5.3 No change needed in `main.cpp`: the synthesized `disconnected()` flips `DE1Device` inactive → `connectedChanged` re-arms the reconnect timer, so a wedged connect can no longer permanently stall the chain (the bare `isConnecting()` guard now always gets released within the watchdog deadline).
- [ ] 5.4 Confirm via the #1303 scenario that the DE1 link recovers without an app restart after a wedge (build/test on device).

## 6. Validation

- [ ] 6.1 Reproduce the absent-scale scenario (saved BT scale powered off) and confirm zero DE1 controller errors / write-failure storms over an extended run.
- [ ] 6.2 Confirm present-scale connect speed at device-picker switch and at app startup is not regressed (still fast).
- [ ] 6.3 Confirm the refractometer reconnect behavior is unchanged.
- [ ] 6.4 Share a test build with the #1303 reporter and confirm the DE1 no longer drops with the scale off.
- [x] 6.5 Build clean via Qt Creator MCP — app + all test targets compiled and linked, 0 errors, 0 warnings. (Fixed a `QTimer::singleShot` member-pointer slot at `main.cpp:1058` that no longer bound after the `bool` param was added.)

## 7. Optional follow-on (separable; not required for #1303)

- [ ] 7.1 Design a single reconnect coordinator that owns the shared `QBluetoothDeviceDiscoveryAgent`, scans while any of {DE1, scale, refractometer} is missing, and connects matching devices on discovery (DE1 priority / backpressure ordering).
- [ ] 7.2 Migrate the three per-device reconnect timers/loops onto the coordinator, removing redundant `startScan()` call sites.
