## 1. Retry bound

- [x] 1.1 Lower `MAX_WRITE_RETRIES` (`src/ble/bletransport.h:133`) from 10 to a flat value in the 3-5 range. Record the census at the definition: 434 retry cycles in the corpus, all recoveries by retry 9, exactly one at retry 10, 380 cycles running the full budget and failing. Flat, not graduated — see design.md.
- [x] 1.2 Check the resulting worst case against the periodic-write interval. At the current constants it is 11 × 5 s + 10 × 0.5 s = 60.0 s against a 60 s MMR keepalive, which is why #1691's link is never idle. Put that arithmetic in the comment.
- [x] 1.3 Add test slots to the existing `tests/tst_bletransporterror.cpp` — do not create a new test file (CLAUDE.md: a new `tst_*.cpp` costs ~1.4 s of build forever, a new slot costs milliseconds). Cover: a write recovering inside the budget completes normally; a write exceeding it is abandoned; the worst-case elapsed time is below the periodic-write interval. Break the bound and watch each go red before keeping it.

## 2. Fault-cluster recalibration

- [ ] 2.1 Re-derive the write-failed fault weight in `QtScaleBleTransport::onDe1LinkFault` (`src/ble/transport/qtscalebletransport.cpp:391-407`). It currently counts a cascade as two faults, justified in-comment as "~5 s of sustained write starvation" from a 10-retry cascade — false at the new budget, and cascades now fire more often.
- [ ] 2.2 Confirm a single write failure cannot latch skip-HIGH app-run-wide unless it genuinely represents sustained starvation. In #1691 one exhaustion demotes every scale to BALANCED for the session (`[4342.561] Write FAILED` → `[4342.579] app-run skip-HIGH latch SET`).
- [ ] 2.3 Record the coupling between the weight and the retry budget where the weight is defined, so a later change to either is not made in ignorance of the other.
- [ ] 2.4 Add test slots for the recalibrated weighting.

## 3. Supersede

- [ ] 3.1 At the upload-supersede point already detected in `DE1Device::startProfileUploadTracking()` (`src/ble/de1device.cpp:1324-1326`), which currently does nothing to the transport queue, discard **the previous upload's** pending writes. This is the narrowest fix for the #1466 cascade.
- [ ] 3.2 Do **not** implement it as `clearQueue()`. Both references supersede per-kind: de1app's `remove_matching_ble_queue_entries` (`de1_comms.tcl:1423`) is called at 14 sites, including `{^Espresso header:}` / `{^Espresso frame #}` at `:1487-88`, always by the producer immediately before enqueueing the replacement; decaid never enqueues the superseded upload at all (`workflow_device_sync.dart:113-127`). A blanket clear also discards unrelated pending work, which is not superseded by anything.
- [ ] 3.3 `m_commandQueue` is `QQueue<std::function<void()>>` (`bletransport.h:126`) and entries carry no metadata, so a selective discard needs the queue to record enough to identify an entry's owner. Scope that first — if it turns out to be large, prefer tracking the upload's writes at the `DE1Device` level over widening the transport's queue type.
- [ ] 3.4 Pair the discard with `m_lastMMRValues` invalidation, as `clearCommandQueue()` already does (`de1device.cpp:1243-1261`) — without it the dedup cache elides the re-send and a discarded setting is lost, not delayed.
- [ ] 3.5 Log the discard and the number discarded.
- [ ] 3.6 Assert, don't build: confirm by test that a commanded stop is delivered across a discard. The current ordering already guarantees it — `stopOperationUrgent`/`sleep` clear before writing (`de1device.cpp:1174`, `:1228`) and `clearQueue()` sets `m_writePending = false` (`bletransport.cpp:399`), so the urgent write takes the direct branch and is never queued at clear time. Neither reference has any priority concept; do **not** add a priority queue.
- [ ] 3.7 Do **not** discard on terminal write failure. Removed from this change after the reference comparison: Decenza today continues to the next command (`bletransport.cpp:148`, `:741`), de1app clears only on connect/disconnect (`de1_comms.tcl:247-248`, `:629-630`), and decaid's clear-on-timeout is about the **platform** operation queue where a stuck entry blocks every following op (`universal_ble_transport.dart:409-415`) — a constraint Decenza's app-level queue does not have. Nothing in the corpus shows harm from the writes behind a failure.
- [ ] 3.8 Tests in `tst_bletransporterror.cpp` / the DE1Device tests: a supersede discards the previous upload's writes; unrelated pending writes survive it; a write abandoned after retries does **not** discard what is queued behind it; a discarded MMR value is actually re-sent afterwards rather than elided; a commanded stop is delivered across a discard.

## 4. Upload retry gating

- [ ] 4.1 Make the ProfileManager retry (`src/controllers/profilemanager.cpp:239-245`) impossible to overlap its predecessor, using an in-flight guard: an attempt is marked outstanding when issued, and the next attempt is scheduled only when that one concludes. This is the shape both references arrive at — de1app because the retry *is* the queue entry re-run in place (`de1_comms.tcl:167-190`), decaid via `_uploading` plus scheduling the backoff timer only after `await setProfile(...)` throws (`workflow_device_sync.dart:113-116`, `:140-141`, `:177-184`).
- [ ] 4.2 Prefer the guard over gating on `queueDrained` with a bounded backstop, which was the earlier plan. Same invariant, one concept instead of three, and no timeout-on-a-condition to justify against the no-timers-as-guards rule. If the guard turns out not to be reachable from ProfileManager, say why before falling back.
- [ ] 4.3 Abandon an outstanding retry sequence when the operation is superseded — decaid's `_generation` guard exists for exactly this and Decenza has no equivalent.
- [ ] 4.4 Test slots in the existing ProfileManager tests: no second attempt is issued while one is outstanding; the next attempt is scheduled from the previous one's conclusion; a supersede abandons the sequence.

## 5. Queue depth observability

- [ ] 5.1 Record when the pending queue passes a depth threshold in `BleTransport::queueCommand()` (`bletransport.cpp:996`), which today has no cap and no warning. de1app warns at 20 (`de1_comms.tcl:49`).

## 6. Consecutive-failure detection (reporting only)

- [ ] 6.1 Count consecutive abandoned writes on the DE1 link at both exhaustion sites, resetting on any successful write and on disconnect. Set the bound at the low end of the observed gap: nine logs peak at 1, #1713 at 2, then 7 / 8 / 11 / 89. Note in the comment that the corpus figure is a proxy that overestimates, since the logs carry no success marker.
- [ ] 6.2 On the bound being passed, emit the log line. Per `docs/CLAUDE_MD/LOGGING.md` this is user-facing, so `INFO` or `WARN` — the connection views default to `minLevel INFO` and a `DEBUG` line would be invisible there.
- [ ] 6.3 Write the line to be self-explanatory: the link stopped accepting writes while still reporting connected, plus the remedy.
- [ ] 6.4 Add **no** teardown on this signal, and **no** `bleError`. Verify by inspection. See design.md Non-Goals for why both are out of scope. Note this is where decaid differs most: its detector tears down (`_declareLinkDead` with `forceOsDisconnect`, `universal_ble_transport.dart:497`). Reporting first is deliberate.
- [ ] 6.5 If the recognition is corroborated against `QLowEnergyController::state()`, an inconclusive answer must change nothing — never treat a failed or ambiguous query as evidence the link is dead (decaid's rule, `universal_ble_transport.dart:466-483`). Qt's state is a weaker probe than the OS query decaid has; if it adds nothing here, skip it and say so.
- [ ] 6.6 Do **not** import decaid's threshold of 3. It counts operations that carry no per-write retries; Decenza's unit is an *exhausted* write, so 3 would be ~97 s. Use the corpus gap from 6.1.
- [ ] 6.7 Do **not** feed this into `evaluateBleWedge`: it gates on `!m_de1Connected` (`blemanager.cpp:356`) and a link in this condition is connected, so it could never fire.
- [ ] 6.8 Test slots: bound passed → recognised; an intervening success resets; a disconnect resets.

## 7. MMR split assurance

- [ ] 7.1 Make `0x803828` (`DE1::MMR::STEAM_FLOW`, `de1characteristics.h:136`) use one assurance level. It is written verified at `src/controllers/maincontroller.cpp:3623` and unverified at `:2722` and `:3562`.
- [ ] 7.2 Note that the verified site's own comment records on-device testing showing zero retries were needed — evidence that argues for levelling down rather than up. Decide deliberately and state the reason at the call site.
- [ ] 7.3 Do **not** route the other MMR call sites through `writeMMRVerified` in this change; unverified is the default side of the split. Neither reference verifies an MMR write at all (de1app `mmr_write`, `de1_comms.tcl:1086`; decaid `_mmrWriteRawPermitted`, `unified_de1.mmr.dart:116`), while both retry MMR **reads** — decaid with its own bounded ladder and a subscribe-before-write ordering that closes the response race (`unified_de1.mmr.dart:55-92`). An MMR read is itself a write — `sendMMRReadRequest()` writes 20 bytes to `a005` (`de1device.cpp:777-785`, props `0x1a`) — so read-back verification adds a second write plus a retry ladder that issues more `a005` writes, on the same contended link. 55 of the corpus's 384 exhaustions are `a005` read requests. `writeMMRVerified` also uses `force=true`, bypassing the #773 dedup cache (`de1device.cpp:1545-1552`).
- [ ] 7.4 Investigate whether the post-connect MMR read burst (GHC info, CPU board model, machine model, firmware version, heater voltage, refill kit — `de1device.cpp:1801`, `:1853-1860`) contributes to reconnect-time fragility. In #1810 all six timed out and retried, each retry issuing another `a005` write onto a link that had just come up. Candidate, not a claim — report findings, change nothing under this task.

## 8. Retire the adapter power-cycle on API 33+

- [ ] 8.1 Gate `BLEManager::setAdapterPower()` and the recovery safety-timer wait so they do not run on `SDK_INT >= 33`. There are **four** call sites (`blemanager.cpp:156`, `:240`, `:397`, `:422`). Do not retire below 33 — see design.md Non-Goals.
- [ ] 8.2 **Name the path that reaches the ladder re-arm.** Every caller of `finishAdapterRecovery` is inside the gated machinery: the safety-timer lambda (`:157`, `:163`) and the host-mode-change handler (`:244`), which only fires because the adapter was powered off. Without a replacement, `bleStackRecovered()` is never emitted, `main.cpp:2599-2605` never resets `de1ReconnectAttempt`, and devices fall from the 60 s tier to `kDE1SlowReconnectMs` = 5 min (`main.cpp:2518`).
- [ ] 8.3 Confirm `m_adapterRecoveryInFlight` cannot latch true with no path to clear it once the adapter leg is gated off.
- [ ] 8.4 Leave the wedge detector, `evaluateBleWedge`, the fault classification and `bleStackRecoveryStarted` untouched — correct, and the only part of this area with test coverage.
- [ ] 8.5 Replace the recovery log lines so they report the true outcome. In #1810 all 100 attempts completed through the never-changed-state fallback and all 100 logged success.
- [ ] 8.6 Remove members left dead on the 33+ path rather than leaving them set-but-unread.

## 9. Teardown gaps

- [ ] 9.1 In `BLEManager::onScaleConnectionTimeout()` (`blemanager.cpp:1826`), widen the teardown so a scan-initiated connect still in `ConnectingState` is torn down. The gate is `wasParked || transport->isConnected()`; a scan-initiated connect satisfies neither, so it sits ~30 s while every retry is rejected as a duplicate (#1810 session 4, t=66.7→96.8).
- [ ] 9.2 In `QtScaleBleTransport::disconnectFromDevice()` (`qtscalebletransport.cpp:149-167`), call `disconnectFromDevice()` on a controller in `ConnectingState` before deleting it. The guard covers only `ConnectedState || DiscoveringState`, then `deleteLater()`s unconditionally — and Qt reaches `BluetoothGatt.close()` only from the `STATE_DISCONNECTED` callback (`QtBluetoothLE.java:290`), which its own comment says may never arrive for a stuck connect (`qlowenergycontroller_android.cpp:140`).
- [ ] 9.3 Test slot: a connect stuck in `Connecting` is torn down at the timeout and the next connect is not rejected as a duplicate.

## 10. Verification

- [ ] 10.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`). Ask before invoking Qt Creator.
- [ ] 10.2 Confirm the qmllint baseline is unmoved — no QML changes expected.
- [ ] 10.3 `gh workflow run android-release.yml --ref <branch> -f upload_to_release=false` so the `Q_OS_ANDROID` paths compile; Linux/macOS CI never builds them.
- [ ] 10.4 `gh workflow run linux-release.yml --ref <branch> -f upload_to_release=false`, and read the run rather than assuming it passed.
- [ ] 10.5 Replay the corpus: confirm the consecutive-failure line would fire on #1810 and #1691 and on none of the nine logs whose maximum run is 1. Confirm the new retry bound would have abandoned the 380 doomed cycles earlier without losing the 37 that recovered by retry 4.

## 11. Close-out

- [ ] 11.1 Re-read #1466, #1468, #1485 and #1586 in light of the queue-stacking fix; comment if it plausibly explains the reported symptom. Note that 1466/1468/1485/c1469 are one SM-T503 session at different truncations, so they are one event, not four.
- [ ] 11.2 Check the wiki manual's connection-troubleshooting wording for any instruction to toggle Bluetooth or reboot that this change makes obsolete.
- [ ] 11.3 Archive with `openspec archive ble-write-retry-and-queue-policy` as the last commit on the branch, before merge.
