# Tasks: suppress-transient-ble-write-popup

## 1. Stop surfacing write-retry exhaustion as a user error

- [x] 1.1 In `src/ble/bletransport.cpp`, remove the `emit errorOccurred("BLE write failed after N retries")` from the write-timeout exhaustion path (~line 115), keeping the warning log, `de1LinkFault("write-failed")`, and queue-continuation logic. Add a brief comment explaining why this is deliberately not user-facing (self-healing; reconnect ladder surfaces persistent failures).
- [x] 1.2 Same removal + comment in the `CharacteristicWriteError` exhaustion path (~line 582).
- [x] 1.3 Check `DE1Transport`/`BleTransport` docs-comments (e.g. `de1transport.h` around the errorOccurred contract) for wording that promises a user-visible error on write failure; update if stale.

## 2. Stale-check queued connection-error popups

- [x] 2.1 In `qml/main.qml` `showNextPendingPopup()`, extend the `bleError` case: if the dequeued popup is a generic connection error (`!isLocationError && !isBluetoothError`) and the DE1 is connected, skip it and call `showNextPendingPopup()` again (mirror the `refill` staleness pattern). Permission errors always show.

## 3. Verification

- [ ] 3.1 Build via Qt Creator and run the full test suite; check whether any test asserts the removed errorOccurred emission (tests around BleTransport/BLE retry) and update expectations if so.
- [ ] 3.2 Ask Jeff to run the app for a normal session (no popup regressions; DE1 connect/disconnect UX unchanged).

## 4. Wrap-up

- [ ] 4.1 Comment on issue #1423 with the diagnosis (overnight silent link drop → keepalive write retry exhaustion → queued stale modal; self-heals in ~25 s) once the fix is merged or in the PR description.
- [ ] 4.2 Archive this OpenSpec change on the feature branch before merge (`/opsx:archive`), PR linked to issue #1423.
