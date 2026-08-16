## Context

See proposal.md — Why, for the defect and the two reference implementations.

The structural facts this design has to work with:

- **Two transport families, three peripheral families.** `BleTransport` owns the DE1's `QLowEnergyController` and a command queue gated by `m_writePending`. `ScaleBleTransport` (implementations `QtScaleBleTransport` and `CoreBluetoothScaleBleTransport`) serves all 14 scale drivers *and* both DiFluid refractometers.
- **Only one of them tracks anything.** `QtScaleBleTransport::writeCharacteristic()` validates the link and calls `service->writeCharacteristic()` straight through. No in-flight flag, no completion matching, no retry, no depth accounting — and the same for `discoverServices`, `discoverCharacteristics`, `enableNotifications` and `readCharacteristic`. That asymmetry is the defect: the DE1 side knows an operation is outstanding and the scale side has no concept of one, so it cannot yield.
- **Qt's own queue is per controller.** `readWriteQueue` and `pendingJob` are instance fields of `QtBluetoothLE`, one instance per `QLowEnergyController`. The darwin backend has no cross-peripheral queue either. No backend supplies the cross-device guarantee, which is why this is unconditional rather than platform-gated.
- **Qt already bounds every operation at 3 s on Android.** `RUNNABLE_TIMEOUT = 3000` (`QtBluetoothLE.java:69`). Our `SUBSCRIBE_TIMEOUT_MS = 3000` is a second clock racing it on the same operation.
- **The failure signal we needed was already being delivered.** `handleOnDescriptorWrite` maps a non-success status to `errorCode = 3` and calls `leDescriptorWritten(...)` — the `DescriptorWriteError` that arrived at +45 ms in the captured session. The code logged it at DEBUG, dropped it, and waited out the full 3 s anyway.
- **Existing DE1 policy that must survive byte-identical.** 50 ms queue pacing, `MAX_WRITE_RETRIES = 5` with a derivation from a 26-log corpus, `WRITE_TIMEOUT_MS = 5000`, `WRITE_RETRY_DELAY_MS = 500`, `writeUrgent`, UUID-scoped `discardQueued()`, the depth warning at 20, and the consecutive-abandonment reporting.
- **The firmware updater holds a link for long stretches**, with erase-and-wait sequences measured in seconds between GATT operations.

## Goals / Non-Goals

**Goals:**

- One queue, one operation dispatched at a time, across every peripheral.
- Give the scale and refractometer transports the in-flight concept they lack.
- Cover connect and service/characteristic discovery, not only reads and writes — the observed collision was a descriptor write against a peripheral in discovery.
- Preserve the DE1's tuned queue behaviour exactly, as per-operation policy rather than queue-wide policy.
- Delete `m_de1WaitTimer` and `SUBSCRIBE_TIMEOUT_MS`. Add no timer.
- One code path on every platform.

**Non-Goals:**

- Not merging the two transport classes. They keep their own platform code; only the queue is shared.
- Not serializing **inbound** notifications. Notifications are deliveries, not operations; gating them would add latency directly to live shot telemetry.
- Not giving scale operations the DE1's retry budget. Their default is zero retries — today's behaviour.
- Not changing the DE1 BLE protocol, the profile upload sequence, or any characteristic's meaning.
- No user-facing setting, diagnostic mode, or way to disable the queue.

## Decisions

### One queue, not a token above two queues

A `BleGattQueue` owns the single operation queue and the in-flight slot. `BleTransport::m_commandQueue` and `m_commandTimer` are deleted; both transport families submit to the shared queue.

*Alternative — keep both queues and gate dispatch with a shared token:* rejected after reading the code. The scale transport has no completion tracking at all, so the token version still has to teach it which platform signal ends which operation — which is most of a queue entry. Having built that, two queues leave ordering decided in three places (each local queue, plus the token) for no gain.

*Alternative — merge the two transport classes outright:* larger, and not required. The queue is the shared part; the platform code is not.

### Per-operation policy, not per-queue policy

Each queued operation carries its own retry budget, pacing and discard key. DE1 operations carry the existing constants unchanged. Scale and refractometer operations default to zero retries.

This is the load-bearing constraint of the whole change. A shared queue with one retry policy would either dilute the DE1's tuned budget or impose it on scales — and the second is actively harmful: a dead scale link would hold the shared slot through the DE1's ~32 s worst-case retry sequence, starving the machine the budget exists to protect.

### Dispatch is driven by the operation's terminal outcome, with no queue-owned clock

The next operation is dispatched when the current one reaches a terminal outcome: its success callback or its error callback. The queue owns no timer.

Where the platform can deliver neither — Android's `handleOnDescriptorWrite` explicitly discards a reply that arrives after Qt's own 3 s timeout, notifying nothing — the release is driven by the requesting transport's existing terminal machinery (`WRITE_TIMEOUT_MS` for writes, `CONNECT_WATCHDOG_MS` for connects). Those already exist, and both verify real state before acting rather than assuming from elapsed time. Adding a queue-owned bound alongside them would recreate the two-clocks-racing condition that produced the 3 s stalls.

### CCCD writes go through the queue like any other operation; `SUBSCRIBE_TIMEOUT_MS` is deleted

`subscribeNext()` currently bypasses the command queue and runs its own timer. Instead it submits a descriptor write, the error is handled when it arrives (+45 ms, not +3000 ms), retry is the queue's bounded retry, and exhaustion produces `noteWriteAbandoned()` → `writeAbandoned` → `de1LinkFault` → failed connect.

Routing it through `de1LinkFault` also closes a gap that would otherwise remain: `evaluateBleWedge()` requires a fault to have fired **and** the scale to be disconnected, so a DE1 stuck mid-subscribe with a scale connected is caught by nothing today.

*Alternative — de1app's unbounded retry:* its own source says retrying a forever-failing vital command "kind of kills the BLE abilities of the app", and it pins the command at the head of a stalled queue. What actually protects de1app is that it issues its enables three separate times during connection setup — redundancy, not error handling.

### Ordering is FIFO with no per-device priority

Served in submission order regardless of peripheral. No DE1 priority.

*Alternative — prioritise the DE1:* rejected. Priority produces starvation, and the problem was concurrency, not ordering. If a starvation case appears, it can be argued on its own evidence.

### Per-operation granularity, not per-phase

The slot is held around each individual operation, not across a whole connect-and-subscribe phase. Phase granularity would have fixed the observed session but leaves steady-state operations racing — a DE1 MMR keepalive and a scale tare issued in the same moment collide identically, with no phase boundary to catch it. It also avoids stalling every peripheral for the duration of a firmware update: the updater holds the slot per operation and releases it across its waits.

### Dispatch the next operation posted, never synchronously from the release

Releasing must not call straight into the next requester on the same stack. A completion handler that releases and immediately re-enters would let a device recurse into its own next operation beneath its own callback — the same re-entrancy class that makes a nested event loop under a QML handler fatal. The next dispatch is posted.

### Delete `m_de1WaitTimer` rather than lengthen it

The 15 s cap is a timer used as a guard, and it covers one of three orderings. Its stated purpose — do not wait forever when no DE1 is present — is already an event: discovery failure, no-device-found, or the existing `CONNECT_WATCHDOG_MS` abort. `setServiceDiscoveryActive()` (the DE1's advisory "peer scales please pause") is subsumed and removed with it.

### The subscribe sequencer collapses into the queue

`subscribeAll()`/`subscribeNext()` is a sequencer in its own right: `m_pendingSubscribeQueue`, `m_currentSubscribeUuid`, a 3 s timeout, and late-ACK matching in `onDescriptorWritten` so a reply for an already-abandoned characteristic cannot advance the chain. Every one of those exists because there was no shared queue to sequence against.

Ported naively it becomes a sequencer running inside a sequencer, which is worse than either. Ported properly it is a loop: submit one operation per stream, then the initial reads, then a final marker operation that baselines the liveness clock and emits `connected()`. FIFO ordering supplies what the recursion used to, and all five of those members and the ACK matcher are deleted.

It also removes a flag that would otherwise have to be invented. A required stream whose retries are exhausted calls `forget(this)`, which drops that requester's remaining queued work — including the completion marker — so "do not report connected" needs no `m_subscribeFailed` bool. The teardown path already says it.

### Two latent defects the migration exposes, both fixes in their own right

- **`characteristicRead` and `characteristicChanged` share one handler.** Under a queue, releasing the slot on an unsolicited notification would free an unrelated write still in the air. They must be separated regardless of this change; today the conflation is merely invisible.
- **Three early returns in `writeCharacteristic()` bail without touching the platform.** Nothing will ever complete for them. Today that is harmless; under a queue each one wedges every device until something else forces a teardown. Every path that does not reach the platform must release the slot.

## Risks / Trade-offs

- **This touches the most carefully tuned code in the BLE layer.** The DE1 write path's retry budget, pacing, urgent path and discard semantics all move. → They must come through byte-identical, and the tests that pin them are load-bearing rather than confirmatory. Pin the current behaviour in tests *before* moving it, so the move is verified against a recorded baseline rather than against a reading of the code.
- **Scale operations gain queueing they never had.** A scale op that never completes now holds the slot where previously it was fire-and-forget. → Zero-retry default plus release-on-teardown keeps the exposure to a single operation, and a disconnecting transport frees the slot immediately.
- **A wedged queue stalls every device, not one.** Concentrating the mechanism concentrates the failure. → This is why release-on-teardown and release-on-error are requirements rather than nice-to-haves, and why an operation released without success is reported as failed rather than assumed successful.
- **Throughput cost on multi-write sequences.** A profile upload is ~20 writes; with a scale connected they now interleave. → The DE1's queue is already paced at 50 ms per write, so the queue is not the binding constraint in the common case. Measure an upload with a scale connected before and after and report the delta rather than asserting no regression.
- **Interleaving during a profile upload.** The DE1 firmware's receive state machine is wedged by a *disconnect* mid-upload, not by delay. → Other peripherals' operations do not disconnect the DE1, and per-device ordering is preserved. Still: exercise an upload with a scale actively connected.
- **Failing the connect on a subscribe error could loop.** A DE1 that consistently fails one CCCD write now reconnects instead of running degraded. → Intended — a machine that cannot chart or stop a shot is not usable — but the reconnect ladder's existing backoff and error surfacing must not be bypassed, and the user must not see a silent reconnect loop.
- **Behaviour change on platforms with no reported defect.** → One log is not evidence other platforms are safe; the per-controller queue is backend-independent, and a single code path is worth more than a platform-conditional optimisation. Accepted deliberately.

## Migration Plan

No persisted state, no schema change, no data migration. Deploy is the build; rollback is a revert.

No CI job builds a PR, so verification is local plus dispatched workflows: the full suite through Qt Creator before opening the PR, an Android test build for the platform the defect was reported on, and a macOS or iOS build to compile `corebluetoothscalebletransport.mm`, which the Android build does not.

## Open Questions

- Whether the DE1's existing 50 ms pacing should be re-tuned once the shared queue is imposing ordering. Deferrable: leaving it as-is is safe, and changing it is an optimisation with its own evidence bar.
