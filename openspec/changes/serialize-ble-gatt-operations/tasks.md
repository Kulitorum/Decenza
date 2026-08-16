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
- [ ] 3.3 Submit `discoverDetails()` through the queue, releasing on `RemoteServiceDiscovered` and on the service-discovery retry path. **Deferred to the group 4 commit, deliberately:** queueing the DE1's discovery only buys anything once the scale's is queued too, and it needs a longer per-operation bound than a write (discovery took 6.0 s in the #1819 capture against `WRITE_TIMEOUT_MS` of 5 s). It lands with its counterpart rather than alone.
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

**NOT STARTED, and deliberately last.** The queue makes this gate harmless rather than
necessary: with GATT operations serialized, a scale connecting during the DE1's
subscribe no longer breaks anything, so deleting the 15 s cap is a connect-latency
improvement, not part of the #1819 fix. It is also the riskiest change left — it
alters startup connect sequencing for every user, and the cap is what stops
`m_de1DirectConnectInFlight` sticking when `de1Discovered` is emitted but main.cpp's
`!isConnecting()` guard declines to start a connect, so no `connectedChanged` ever
fires to release it. Replacing it needs an event that covers that case, and getting it
wrong strands the scale rather than delaying it.

- [ ] 6.1 Delete `m_de1WaitTimer`, `m_scaleConnectDeferred` and the 15 s branch in `tryDirectConnectToScale()` in `src/ble/blemanager.cpp`. The release event has to cover "the DE1 connect was never actually started", which `onDe1ConnectionSettled()` does not.
- [ ] 6.2 Remove `setServiceDiscoveryActive()` and its call sites, now subsumed by the shared queue.
- [ ] 6.3 Confirm the no-DE1-present case still connects a scale promptly, driven by discovery failure / `CONNECT_WATCHDOG_MS` rather than by an elapsed-time cap.

## 7. Observability

- [x] 7.1 Raise the descriptor-error record to the tier the connections views display, and include the characteristic UUID so the failed stream is named rather than inferred.
- [x] 7.2 Add a single record at the end of DE1 notification subscription stating which streams are live. (The ready marker is the place for it.)
- [x] 7.3 Re-read `docs/CLAUDE_MD/LOGGING.md` tier rules against every line added — audience, not importance, picks the tier.

## 8. Tests for the new behaviour

- [x] 8.1 Two requesters: the second is not dispatched until the first reaches a terminal outcome; each requester's own order preserved.
- [x] 8.2 An operation that errors releases the slot at the error, not later, and is reported as failed to its requester.
- [x] 8.3 A requester torn down while holding the slot frees it immediately and its queued operations are discarded; another requester proceeds.
- [x] 8.4 A zero-retry operation is not retried; a DE1 operation retries exactly its configured budget.
- [ ] 8.5 A DE1 connect whose required-stream CCCD write fails does not emit `connected()`. Break the fix and watch this go red before keeping it. Not yet written: it needs a fake service, which no test in this tree has.
- [x] 8.8 The single-in-flight invariant is asserted by a test that can fail. Verified by removing the guard — and the FIRST attempt did not go red, because the check is duplicated in `scheduleDispatch()` and `dispatchNext()` and neither alone is load-bearing, and because a submit arriving while the dispatch timer is still armed is held back by the timer without consulting the check at all. Both tests were rewritten to submit after the previous operation is already in flight; they now go red with the guards removed and green with them restored.
- [ ] 8.6 An optional-stream failure still yields `connected()`.
- [x] 8.7 Put shared production sources into a narrow intermediate library if more than one test target needs them — never into `decenza_testlib` (`scripts/check_test_source_duplication.py` gates this).

## 9. Verify on hardware

- [ ] 9.1 Reproduce the original ordering: force a slow DE1 connect with a scale present, confirm all five DE1 subscriptions complete and a shot charts and stops at weight.
- [ ] 9.2 Connect a DiFluid refractometer mid-session while the DE1 is connected and delivering telemetry; confirm neither link is disturbed.
- [ ] 9.3 Re-run task 1.3's profile-upload measurement with a scale connected and report the delta.
- [x] 9.4 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`) before opening the PR — nothing on GitHub runs it automatically.
- [x] 9.5 Dispatch an Android CI test build (`gh workflow run android-release.yml --ref <branch> -f upload_to_release=false`).
- [x] 9.6 Dispatch a macOS or iOS CI build to cover `corebluetoothscalebletransport.mm`, which the Android build does not compile.

## 10. Documentation and close-out

- [x] 10.1 Add the shared queue to `docs/CLAUDE_MD/BLE_PROTOCOL.md`, recording that Qt's GATT queue is per-controller (`QtBluetoothLE.java:949-950`) so the cross-device guarantee is the app's to supply, and that scale transports previously had no in-flight tracking at all.
- [x] 10.2 Record why `SUBSCRIBE_TIMEOUT_MS` was deleted rather than tuned, and why scale operations do not inherit the DE1's retry budget.
- [ ] 10.3 Assess whether the wiki manual needs an entry — the user-visible change is that a half-connected DE1 now shows as disconnected and reconnects instead of appearing ready. A few sentences if so.
- [ ] 10.4 Archive the change with `openspec archive serialize-ble-gatt-operations` as the last commit on the branch, before merge.
