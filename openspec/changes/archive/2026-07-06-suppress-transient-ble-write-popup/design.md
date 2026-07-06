# Design: suppress-transient-ble-write-popup

## Context

Diagnosed from the debug log on [#1423](https://github.com/Kulitorum/Decenza/issues/1423): overnight, the only DE1 traffic is the 60 s charger keepalive (`setUsbChargerOn` → WriteToMMR `a006`). When the link dies silently, that write exhausts 10 retries (~5 s), `BleTransport` emits both `errorOccurred("BLE write failed after 10 retries")` and `de1LinkFault("write-failed")`, the controller then reports `Unconnected`, and the reconnect ladder restores the link on attempt 1 (~25 s end-to-end). The `errorOccurred` reaches main.qml's `bleErrorDialog` via `BLEManager::onDe1Error`; with the screensaver active it is queued and shown at wake — hours stale, contradicting the "Ready" status bar. Two independent occurrences in the log, both self-healed.

Error-surfacing chain: `BleTransport::errorOccurred` → `DE1Device` → `BLEManager::onDe1Error` (debounce per distinct message, cleared on reconnect) → `BLEManager::errorOccurred` → main.qml `onErrorOccurred` → `bleErrorDialog.open()` or `queuePopup("bleError", ...)` when `screensaverActive`.

## Goals / Non-Goals

**Goals**
- No modal for a self-healing DE1 write failure; keep every diagnostic (log warning, `de1LinkFault`, fault-cluster latch, reconnect trigger).
- Queued generic connection errors that outlive the problem are dropped at display time.

**Non-Goals**
- No change to retry counts, timeouts, reconnect ladder, or the dual-HIGH fault-cluster detection (`ble-connection-priority` spec) — `de1LinkFault` emission is untouched.
- No change to scale error UX (`ScaleBleTransport` stack is separate from `BleTransport`, which is DE1-only).
- Not attempting to fix the underlying overnight link drop — it's tablet-stack behavior, already handled by auto-reconnect.

## Decisions

1. **Remove the `errorOccurred` emission at the source (both exhaustion sites in `BleTransport`), rather than filtering by message string downstream.**
   `BLEManager::onDe1Error` and main.qml receive many error kinds; string-matching "write failed" there is brittle and leaves the dead emission in place. The transport knows precisely which event this is. Persistent failures remain covered because the reconnect path emits its own connection errors (the comment on `onDe1Error` documents that ladder behavior), and the machine-status UI shows Disconnected.

2. **Stale check at dequeue, mirroring the `refill` precedent.**
   `showNextPendingPopup()` already skips a stale `refill` popup when the phase moved on. Apply the same pattern to `bleError`: skip when the popup is a generic connection error (`!isLocationError && !isBluetoothError`) and `DE1Device.connected` is true, then recurse to the next pending popup. Permission errors still require user action, so they always show. This also covers other transient connection-class errors queued behind the screensaver, not just write failures.

3. **Keep the transport-level warning log lines unchanged.**
   Triage of future reports depends on the `FAILED after 10 retries` log signature (it's what made this diagnosis possible).

## Risks / Trade-offs

- [A genuinely wedged system where writes fail but the controller never reports disconnect would now show no modal] → The write-failure path already force-continues the queue and the wedge detector (`evaluateBleWedge` via `de1LinkFault`) plus the reconnect ladder handle escalation; the ladder's errors surface. If a gap is ever observed in practice, surfacing belongs there, not on the per-write path.
- [Dropping a stale queued `bleError` could hide a still-relevant message if the link flaps (reconnected at dequeue, drops again seconds later)] → The next failure re-emits through the normal path; the debounce in `onDe1Error` is cleared on reconnect, so nothing is permanently suppressed.

## Migration Plan

Behavioral change only (one C++ file, one QML file); ships in a normal release, trivially revertable.

## Open Questions

None.
