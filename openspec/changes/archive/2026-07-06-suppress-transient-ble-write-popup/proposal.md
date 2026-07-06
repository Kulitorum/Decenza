# Suppress transient BLE write-failure popup

## Why

Every morning, users wake their tablet to a modal "Connection Error: BLE write failed after 10 retries" — while the status bar above it says Ready ([#1423](https://github.com/Kulitorum/Decenza/issues/1423)). The debug log shows why: overnight the BLE link dies silently, the next 60-second charger keepalive write to WriteToMMR exhausts its 10 retries, the app emits a user-visible error, then auto-reconnects successfully ~25 seconds later. Because the screensaver is active, the popup is queued and greets the user hours after the problem self-healed. The reporter has seen this "for the past 10 or 15 versions" — it is the long-standing surfacing of a routine, self-recovering link drop, not a recent regression.

## What Changes

- `BleTransport` no longer emits the user-visible `errorOccurred("BLE write failed after N retries")` when write retries are exhausted (both the write-timeout and `CharacteristicWriteError` paths). The log warning and the `de1LinkFault("write-failed")` signal are kept — they drive the auto-reconnect, the fault-cluster latch, and diagnostics.
- Persistent-failure coverage is unchanged: if reconnection keeps failing, the reconnect ladder's own "Connection error" still surfaces to the user (debounced per distinct message in `BLEManager::onDe1Error`), and the UI shows the Disconnected state.
- Staleness guard for queued popups: when `showNextPendingPopup()` dequeues a `bleError` that is a generic connection error (not a Location/Bluetooth-permission call-to-action), it is skipped if the DE1 has since reconnected — the same stale-check pattern the `refill` popup already uses.

## Capabilities

### New Capabilities
- `ble-error-surfacing`: which BLE failures surface as user-facing modals vs. log-only + auto-recovery; staleness rules for popups deferred by the screensaver queue.

### Modified Capabilities

(none — `ble-connection-priority` consumes `de1LinkFault`, which is unchanged; its "write-failed cascade counts as 2 faults" requirement is unaffected)

## Impact

- `src/ble/bletransport.cpp` — two `emit errorOccurred(...)` sites removed (write-timeout path, `CharacteristicWriteError` path); `de1LinkFault` + warnings stay.
- `qml/main.qml` — `bleError` case in `showNextPendingPopup()` gains a stale check against DE1 connectivity.
- No settings, no BLE protocol change, no change to reconnect behavior or fault-cluster detection.
- Scope is DE1-only: `BleTransport` is the DE1 transport (`class BleTransport : public DE1Transport`); scales use the separate `ScaleBleTransport` stack with its own dialog/reconnect UX, untouched here.
