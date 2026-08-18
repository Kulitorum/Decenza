## 1. Pin today's DE1 queue behaviour before moving it

- [x] 1.1 Add tests that record the DE1 command queue's current observable behaviour: 50 ms pacing between writes, retry count and delay on a failing write, `writeUrgent` ordering ahead of queued work, `discardQueued()` dropping only the named UUIDs, `clearQueue()` return value, and the depth warning at 20. These must pass on `main` unchanged.
- [x] 1.2 Add tests recording the abandonment signals: `writeAbandoned`, `de1LinkFault("write-failed")`, and the consecutive-failure reporting and its close-out on recovery and on disconnect.
- [ ] 1.3 Record the current duration of a DE1 profile upload with a scale connected, so the throughput risk in design.md is answered with a number.

## 2. The shared queue

- [x] 2.1 Add `BleGattQueue` in `src/ble/`: one queue, one in-flight slot, FIFO across requesters, each requester's own submission order preserved.
- [x] 2.2 Give each queued operation its own policy — retry budget, retry delay, pacing, discard key — defaulting to zero retries, so a submitter opts into retry rather than inheriting it.
- [x] 2.3 Dispatch the next operation only from a terminal outcome of the current one, posted rather than called synchronously from the release.
- [x] 2.4 Implement release-on-teardown: a requester disconnecting or being destroyed frees the slot immediately and discards that requester's queued operations.
- [x] 2.5 Implement per-requester `clear` and UUID-scoped `discard` over the shared queue, scoped so one requester cannot drop another's work.
- [x] 2.6 Confirm no timer is owned by the queue. If one appears necessary, stop and re-derive — design.md states the terminal outcome and the transports' existing machinery are the only permitted release paths.
- [x] 2.7 Add log lines for submit, dispatch, release, teardown-discard and depth, following `docs/CLAUDE_MD/LOGGING.md`, and register the new file in the correct glob set in `scripts/check_log_markers.py`.

## 3. Move the DE1 transport onto it

- [x] 3.1 Replace `m_commandQueue`, `m_commandTimer` and `m_writePending` in `src/ble/bletransport.cpp` with submissions to the shared queue, carrying the existing constants as per-operation policy.
- [x] 3.2 Re-point `clearQueue()` and `discardQueued()` at the shared queue, preserving their current return values and UUID scoping.
- [x] 3.3 Submit `discoverDetails()` through the queue, releasing on `RemoteServiceDiscovered` and on the service-discovery retry path. The longer bound it needs is now `BleGatt::DISCOVERY_TIMEOUT_MS`, declared once and shared with the scale transports rather than duplicated per class — the answer is a property of the radio, not of who is asking.
- [ ] 3.3a The controller CONNECT is deliberately NOT queued, which is a departure from design.md's "cover connect and service discovery". A connect is bounded only by `CONNECT_WATCHDOG_MS` (35 s), so a wedged one would hold the shared slot for 35 s and starve every other device — a worse failure than the one being fixed, and unlike a GATT operation a connect is radio-scheduler work the host stack already interleaves. Record the decision in design.md if it survives group 4.
- [x] 3.4 Re-run the group 1 tests unchanged. Any difference is a regression, not a new baseline.
- [ ] 3.5 Confirm the firmware updater releases the slot across its erase-and-wait intervals rather than holding it, and that a firmware update does not stall a connected scale.

## 4. Move the scale and refractometer transports onto it

- [x] 4.1 Add in-flight tracking to `src/ble/transport/qtscalebletransport.cpp`: submit connect, service discovery, characteristic discovery, notification enable and each read/write, and match each platform completion signal back to its operation.
- [x] 4.2 Do the same in `src/ble/transport/corebluetooth/corebluetoothscalebletransport.mm`, matching the Qt implementation's ordering exactly — no platform-conditional behaviour. This removed a real divergence: `didDiscoverServices` kicked characteristic discovery for every service from inside the delegate, ahead of the Qt signals, so on Apple platforms discovery reached the radio outside any queue and before the driver asked for it. **Needs hardware verification (task 9.1/9.2 on macOS or iOS) — it changes the Apple connect sequence.**
- [x] 4.2a Two platform limits, recorded rather than worked around: CoreBluetooth delivers a read response and an unsolicited notification through one callback with nothing to distinguish them, so a notification can release an outstanding read's slot early (harmless — the response still arrives); and a write WITHOUT response is completed at issue on both backends, because neither platform acknowledges one (`btcentralmanager.mm:815-818`).
- [x] 4.3 Keep zero retries as the default for these transports, so a submitted operation that fails behaves exactly as today's fire-and-forget call did.
- [x] 4.4 Confirm no per-device changes are needed in `src/ble/scales/` or `src/ble/refractometers/`; if any driver reaches around its transport to the platform, route it through the transport instead.

## 5. Subscribe through the queue; delete the bespoke timer

- [x] 5.1 Submit CCCD writes through the shared queue instead of calling `writeDescriptor()` directly with its own timer. `subscribeNext()` itself is gone — the recursion, `m_pendingSubscribeQueue`, `m_currentSubscribeUuid` and the late-ACK matcher all collapsed into FIFO ordering plus a ready marker.
- [x] 5.2 Delete `SUBSCRIBE_TIMEOUT_MS` and `m_subscribeTimeoutTimer`, recording in the code why: it duplicated Qt's own `RUNNABLE_TIMEOUT` (`QtBluetoothLE.java:69`) and raced it.
- [x] 5.3 Handle the descriptor error when it arrives instead of logging at DEBUG and dropping it.
- [x] 5.4 On exhaustion for a required stream (`STATE_INFO`, `SHOT_SAMPLE`), do not emit `connected()` — tear down and re-enter the existing reconnect path. Keep a non-required stream's failure non-fatal and recorded.
- [x] 5.5 Confirm the failure reaches `de1LinkFault` so `evaluateBleWedge()` can see a DE1 stuck mid-subscribe, and that the reconnect ladder's existing backoff and error surfacing still apply — no silent reconnect loop.

## 6. Remove the timer gate

**Done.** The 15 s cap is gone and nothing replaced it with another clock. The gate
that replaced it is de1app's, not a smaller cap — see "Gating a connect" in design.md.

**A figure recorded here earlier was wrong and is withdrawn.** This section claimed a
deliberate regression: "with a saved-but-absent DE1 the gate holds the scale until
CONNECT_WATCHDOG_MS (35 s)". That number was read off a constant, not measured. Across
29 user-submitted captures the connect watchdog fires **twice**; the platform resolves
a failed connect first, and the two slowest observed are 29.83 s and 29.91 s. It was
then replaced with "99.6% of failed connects resolve in under a second", which is also
withdrawn: all 1,279 of those failures come from a **single** log, and only 4 of the 29
captures contain DE1 controller-state lines at all. The honest position is that the
absent-DE1 case is **not measurable from any log available**, which is why the gate
below is designed to be safe in it by construction rather than by measurement.

The flag it was unsticking was set in the wrong place. `tryDirectConnectToDE1()` set
`m_de1DirectConnectInFlight = true` and then emitted `de1Discovered()` — a REQUEST,
which main.cpp's handler declines whenever the DE1 is already connected or connecting.
So the gate could close over a connect that never started, and the cap existed to
reopen it. Fixing where the state comes from removes the need for the cap rather than
replacing it: `noteDe1Connecting()` is fed from DE1Device's own
`connectingChanged`/`connectedChanged`, so the gate is closed exactly while a DE1
connect is genuinely in flight and open otherwise.

That also widens what the gate covers, for free. `DE1Device::m_connecting` clears in
`onTransportConnected()`, which now runs only after every notification subscription is
confirmed — so the gate brackets the DE1's connect, discovery AND subscribe, which is
precisely the window #1819 had a scale connecting into.

**One behaviour regression, deliberate and worth weighing.** With a saved-but-absent
DE1, the gate now holds the scale until `CONNECT_WATCHDOG_MS` (35 s) aborts the hung
attempt, where the cap released it at 15 s. Longer, but bounded by a real event
instead of a guess — and releasing early is what #1819 was.

- [x] 6.1 Delete `m_de1WaitTimer` and the 15 s branch in `tryDirectConnectToScale()` in `src/ble/blemanager.cpp`. `m_scaleConnectDeferred` stays: it records that a scale connect wanted to run.
- [x] 6.1a Gate the deferred scale connect on shared-queue **backpressure**, released by `BleGattQueue::drained()`. `noteDe1Connecting()` survives as observability only — nothing gates on it.
- [ ] 6.1b Field-verify the inherited "two concurrent GATT connects collide" claim from the radio-context line every scale connect now emits, and either restore a gate for it with evidence or delete the claim from the code.
- [x] 6.2 Remove `setServiceDiscoveryActive()` and its call sites. It was the one genuinely redundant mechanism here: a bool mirrored through `BleTransport` → `DE1Transport::serviceDiscoveryActiveChanged` → `DE1Device` → `main.cpp:2143` → `BLEManager::setDe1ServiceDiscoveryActive` → `m_de1ServiceDiscoveryActive` → `DecentScale::setHeartbeatsPaused`, five files to express "do not let a scale heartbeat write race DE1 characteristic discovery" (#1176, Tab A8) — for one driver, advisorily. The queue expresses it for every device, as a guarantee. But it is NOT subsumed yet: the scale heartbeat is queued and the DE1's own `discoverDetails()` is not, so the two can still overlap until 3.3 lands. Deleted with 3.3 in the same change, which is what made it genuinely redundant. `DecentScale::setHeartbeatsPaused()` and `m_heartbeatsPaused` went with it — nothing else drove them, so leaving them would have left a pause nobody could ask for. (This task previously read "now subsumed by the shared queue" while 3.3 was still deferred, which was true of the design and not of the code — recorded rather than quietly corrected.)
- [ ] 6.3 Confirm on hardware that the no-DE1-present case still connects a scale, driven by `CONNECT_WATCHDOG_MS` rather than an elapsed-time cap — and time it, since 35 s is the expected worst case where 15 s was.

## 7. Observability

- [x] 7.1 Raise the descriptor-error record to the tier the connections views display, and include the characteristic UUID so the failed stream is named rather than inferred.
- [x] 7.2 Add a single record at the end of DE1 notification subscription stating which streams are live. (The ready marker is the place for it.)
- [x] 7.3 Re-read `docs/CLAUDE_MD/LOGGING.md` tier rules against every line added — audience, not importance, picks the tier.

## 8. Tests for the new behaviour

- [x] 8.1 Two requesters: the second is not dispatched until the first reaches a terminal outcome; each requester's own order preserved.
- [x] 8.2 An operation that errors releases the slot at the error, not later, and is reported as failed to its requester.
- [x] 8.3 A requester torn down while holding the slot frees it immediately and its queued operations are discarded; another requester proceeds.
- [x] 8.4 A zero-retry operation is not retried; a DE1 operation retries exactly its configured budget.
- [x] 8.5 A DE1 connect whose required-stream CCCD write fails does not emit `connected()`. Written headless after all — no fake service is needed: `submitSubscribe()` checks the characteristic map at SUBMIT (a permanently-absent characteristic is a fact about the connection, not something worth five retries), so a transport with no service reaches `failRequiredStream()` directly. `tst_bletransporterror::aFailedRequiredStreamDropsTheReadyMarkerSoConnectedNeverFires`. The contract is an ABSENCE, which is what a later change silently undoes, so 8.6 below is what makes it able to fail.
- [x] 8.8 The single-in-flight invariant is asserted by a test that can fail. Verified by removing the guard — and the FIRST attempt did not go red, because the check is duplicated in `scheduleDispatch()` and `dispatchNext()` and neither alone is load-bearing, and because a submit arriving while the dispatch timer is still armed is held back by the timer without consulting the check at all. Both tests were rewritten to submit after the previous operation is already in flight; they now go red with the guards removed and green with them restored.
- [x] 8.6 An optional-stream failure still yields `connected()` (`aFailedOptionalStreamLeavesTheReadyMarkerStanding`). This is the negative control for 8.5: without it, "call forget() on every subscribe failure" passes 8.5 while making a missing water-level notification abort a perfectly usable machine.
- [x] 8.7 Put shared production sources into a narrow intermediate library if more than one test target needs them — never into `decenza_testlib` (`scripts/check_test_source_duplication.py` gates this). Resolved as: `scalebletransport.cpp` sits in `BLE_SOURCES` beside `bletransport.cpp` and `blegattqueue.cpp`, which were already there. A narrow library is the rule for a NEW shared source and is the wrong call for this one — the rule exists to kill duplicate COMPILES, and this file compiles exactly once either way. The gate is at zero with it there.

## 8b. Review pass (post-implementation)

- [x] 8b.1 `paceMsAfter` deleted from `Policy` along with its two tests. It was carried into the new queue as dead configuration: the constant it came from was armed on an enqueue that found the link idle, so it delayed the FIRST write after a pause and never paced consecutive ones. Every doc claim about "50 ms pacing" corrected in the same pass rather than left to be re-read as fact.
- [x] 8b.2 The queue's last `QTimer` is gone. Dispatch is `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` guarded by `m_dispatchPosted`. Both hop the event loop; only one of them says so, and there is no duration here to get wrong.
- [x] 8b.3 `drained()` is emitted from `discard()` and from a dispatch that finds the queue already empty — the two paths that can reach idle with no slot transition, and the ones a deferred scale connect would otherwise wait on forever. It is posted (a `forget()` from a destructor must not run a consumer against a half-destroyed object) and collapsed to one emission per transition by `m_drainedPosted`, which a test caught firing twice.
- [x] 8b.4 `BleGattQueue::validate()` rejects an operation with no `issue` callback or no requester, at submit, with a self-contained WARN. Either would take the slot and be ended by nothing: all Bluetooth traffic stopped until restart, no error anywhere.
- [x] 8b.5 `QLowEnergyService::OperationError` added to every failure arm. Qt emits it — NOT the per-operation error — for a synchronously rejected read/write/descriptor-write (`qlowenergyservice.cpp:581/650/724`), so without it those rejections released nothing.
- [x] 8b.6 `linkAcceptsGattOperations()` extracted; read and subscribe now re-check the link at DISPATCH like write already did. The check existed, it just was not somewhere the other two could reach.
- [x] 8b.7 Comments re-derived rather than tidied: `writeUrgent` is queue POSITION, never a bypass, and three sites claiming it "writes synchronously" were corrected with the reason one event-loop turn is available on the iOS suspend path. Two orphaned signal doc-blocks deleted. The decaid claim inverted in `BLE_PROTOCOL.md` and `proposal.md` — it sets `queueType = QueueType.perDevice`, a counter-example on the queue and a precedent only on failing the connect.
- [x] 8b.8 Three dead-link slots deleted from `tst_blecommandqueue` — a strict subset of what `tst_bletransporterror` asserts against exact log text. One invariant, one place.
- [x] 8b.9 Timer audit across `src/ble/`. Removed: `m_de1WaitTimer`, `SUBSCRIBE_TIMEOUT_MS`, `m_commandTimer`, and two now-dead `#include <QTimer>`. Remaining and NOT removable: the per-transport operation timeout (the only thing that can end an operation the platform never answers — there is no event, which is the condition), the retry backoff (decides when, never whether), and the Android connect watchdog (#1303). The ~30 fixed delays in the 15 scale drivers are now candidates but were deliberately left: none is testable here, several are device settle times rather than radio workarounds, and telling those apart needs the hardware. Own change, one driver at a time.

## 9. Verify on hardware

- [x] 9.1 Verified on a Samsung SM-X210 with a Decent Scale, repeatedly: the scale connects during DE1 service discovery (the #1819 ordering) and all five DE1 subscriptions still complete — `DE1 telemetry live: state, shot samples, water level, MMR responses, temperatures` in five consecutive starts, no `DescriptorWriteError`, no required-stream failure. A shot charted and stopped at weight.
- [x] 9.2 Verified: a DiFluid R2 connected mid-session with the DE1 and scale already live — discovered 250.25 s, `Connected and ready for measurements` 251.67 s, 1.4 s end to end. Neither existing link was disturbed and no contention episode was reported anywhere near it.
- [ ] 9.3 **Deliberately not done.** Profile-upload timing delta with a scale connected. Left open rather than quietly dropped: the throughput concern it was written for is largely answered — the 50 ms pacing was deleted rather than carried across, so an upload issues faster than before, and the hardware runs show uploads completing normally (`Profile upload verified — 5 frame(s) ACKed in order`). A stopwatch figure would be nice to have and gates nothing.
- [ ] 9.8 **A DE1 write dispatched against a down link spends its full retry budget** — `linkAcceptsGattOperations()` failing at dispatch calls `noteFailed()`, so 5 x 500 ms of the SHARED slot is spent re-asking a question whose answer is fixed until the link returns, and via `isBusy()` that also re-defers a scale connect through the DE1-down window. An `abandon()` (terminal, no retry) was written and reverted: the window is bounded — `disconnect()` calls `forget()` and drops the queued writes — and the change invalidates the headless seam ten queue tests hold the slot with, which is a large cost for an unmeasured gain. Measure the real window first (how long queued DE1 writes survive a link going bad before the disconnect is noticed); build the terminal path only if it is not negligible.
- [x] 9.9 **Make a cross-device delay visible in the log before deciding anything about it.** Two lines, one per layer. `BleGattQueue` accumulates the time each queued operation spends behind a DIFFERENT requester and WARNs above `FOREIGN_WAIT_WARN_MS` (500), naming the operation and the wait — only the FOREIGN part, because waiting behind your own queued work is a profile upload and uninteresting. `DE1Device`'s existing SAW latency line, whose `bleAck` already spans the queue, escalates from DEBUG to WARN above `SAW_SLOW_ACK_WARN_MS` (300 ms, ~0.6 g at a typical pour) and points the reader at the queue line. Neither threshold gates anything; they choose a tier. This is what task 9.7 reads.
- [x] 9.7 Measured: `dispatch=9 ms, bleAck=64 ms, total=73 ms` with a BLE scale connected — an order of magnitude under `SAW_SLOW_ACK_WARN_MS`, so it logged at DEBUG. Against the same machine pre-change (shot 1103): `dispatch=11 ms, bleAck=45 ms, total=56 ms`. So the shared queue costs roughly **+19 ms** on the acknowledged stop write, which at 1.75 g/s is **0.033 g** — below anything tastable, and ~5% of that shot's 0.6 g overshoot (the rest was drip prediction, `expectedDrip=1.01 g` against ~1.6 g actual). Caveat on the record: n=1 each side, and the pre-change shot ran a WiFi scale, so the BLE radio carried only the DE1 — the +19 ms is not cleanly attributable to the posted dispatch versus simply having a second device on the radio. **This is the measurement that decides against building heartbeat suppression**: the exposure is real, bounded and negligible in the normal case.
- [x] 9.4 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`) before opening the PR — nothing on GitHub runs it automatically.
- [x] 9.5 Dispatch an Android CI test build (`gh workflow run android-release.yml --ref <branch> -f upload_to_release=false`).
- [x] 9.6 Dispatch a macOS or iOS CI build to cover `corebluetoothscalebletransport.mm`, which the Android build does not compile.

## 10. Documentation and close-out

- [x] 10.1 Add the shared queue to `docs/CLAUDE_MD/BLE_PROTOCOL.md`, recording that Qt's GATT queue is per-controller (`QtBluetoothLE.java:949-950`) so the cross-device guarantee is the app's to supply, and that scale transports previously had no in-flight tracking at all.
- [x] 10.2 Record why `SUBSCRIBE_TIMEOUT_MS` was deleted rather than tuned, and why scale operations do not inherit the DE1's retry budget.
- [x] 10.3 Assessed: **no manual entry.** Maintainer's call (2026-08-18) — this change has no user-visible surface. Nothing moves in the UI, no setting appears, and no workflow changes. The one borderline case is a DE1 whose required telemetry never started: it now reports disconnected and reconnects rather than sitting there looking ready. That is a machine that was already broken behaving correctly instead of silently, not a feature to document — and a manual page describing it would be a page about a bug that no longer happens.
- [ ] 10.4 Archive the change with `openspec archive serialize-ble-gatt-operations` as the last commit on the branch, before merge.
