## Why

Notices waited behind the screensaver but not behind a running operation. Any of these could open, modal and dimmed, over the page whose Stop button a headless machine needs (Espresso, Steam, Hot Water, Flush):
- the update prompt
- the charging-mismatch warning
- the scale notices
- BLE errors
- the local-network notice

Waking from the group head made this certain. The scale-notice deferral is released at EspressoPreheating and drains the queue right then, so "No scale detected" opened just as the shot began.

## What Changes

- `machineOperating` holds a notice back. It covers the operation phases plus steam warm-up, when the DE1 is in its Steam state while MachineState still reports Heating. A firmware flash is not included: it has no Stop button, and its AwaitingReboot state can last indefinitely.
  - `operationActive` stays as it was for the sleep and auto-load timers, now built on `machineOperating`.
- When an operation starts, notices already open are closed and re-queued, the same way the screensaver already handles them.
- When an operation ends, the queue drains, but only once no other dialog is up. The drain is deferred a tick so refill and standby prompts open first.
  - Every dialog `anyModalDialogVisible()` lists now drains the queue on close.
- A BLE error raised during an operation keeps that fact through every hop (dequeue, direct open, re-queue), and is never dropped as a stale DE1 connection error. Losing the DE1 ends the operation, so such an error comes from a scale or refractometer.
- A permission BLE error takes over a queued generic one instead of being deduplicated away.
- The decent-machine choice is queued during an operation. A recipe activation failure is not shown then: the caller is remote and gets the failure in its own response.

## Capabilities

### New Capabilities
- `notice-deferral`: unrelated notices wait for the end of an operation the same way they wait for the screensaver.

### Modified Capabilities
- `ble-error-surfacing`: a queued error raised during an operation is exempt from the stale-connection skip.

## Impact

- **Code:** `qml/main.qml` only.
- **Tests:** none at unit level, because the queue lives in `main.qml`, which no test loads. CI's QML diagnostics gate and the full suite cover the rest.
