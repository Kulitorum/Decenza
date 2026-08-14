## 1. Retry bound

- [ ] 1.1 Lower `MAX_WRITE_RETRIES` (`src/ble/bletransport.h:133`) from 10 to a flat value in the 3-5 range. Record the census at the definition: 434 retry cycles in the corpus, all recoveries by retry 9, exactly one at retry 10, 380 cycles running the full budget and failing. Flat, not graduated — see design.md.
- [ ] 1.2 Check the resulting worst case against the periodic-write interval. At the current constants it is 11 × 5 s + 10 × 0.5 s = 60.0 s against a 60 s MMR keepalive, which is why #1691's link is never idle. Put that arithmetic in the comment.
- [ ] 1.3 Add test slots to the existing `tests/tst_bletransporterror.cpp` — do not create a new test file (CLAUDE.md: a new `tst_*.cpp` costs ~1.4 s of build forever, a new slot costs milliseconds). Cover: a write recovering inside the budget completes normally; a write exceeding it is abandoned; the worst-case elapsed time is below the periodic-write interval. Break the bound and watch each go red before keeping it.

## 2. Fault-cluster recalibration

- [ ] 2.1 Re-derive the write-failed fault weight in `QtScaleBleTransport::onDe1LinkFault` (`src/ble/transport/qtscalebletransport.cpp:391-407`). It currently counts a cascade as two faults, justified in-comment as "~5 s of sustained write starvation" from a 10-retry cascade — false at the new budget, and cascades now fire more often.
- [ ] 2.2 Confirm a single write failure cannot latch skip-HIGH app-run-wide unless it genuinely represents sustained starvation. In #1691 one exhaustion demotes every scale to BALANCED for the session (`[4342.561] Write FAILED` → `[4342.579] app-run skip-HIGH latch SET`).
- [ ] 2.3 Record the coupling between the weight and the retry budget where the weight is defined, so a later change to either is not made in ignorance of the other.
- [ ] 2.4 Add test slots for the recalibrated weighting.

## 3. Queue drain

- [ ] 3.1 Drain the pending queue at the upload-supersede point already detected in `DE1Device::startProfileUploadTracking()` (`src/ble/de1device.cpp:1324-1326`), which currently does nothing to the transport queue. This is the narrowest fix for the #1466 cascade.
- [ ] 3.2 **Carve out urgent state writes.** `BleTransport::writeUrgent()` prepends to `m_commandQueue` when a write is in flight (`src/ble/bletransport.cpp:227-231`), and stop-at-weight and sleep both route `REQUESTED_STATE` through it (`de1device.cpp:1186`, `:1235`). An unqualified drain can discard a commanded stop. Either mark urgent entries so a drain skips them, or route urgent state writes outside the queue entirely.
- [ ] 3.3 Pair every drain with `m_lastMMRValues` invalidation, as `clearCommandQueue()` already does (`de1device.cpp:1243-1261`) — without it the dedup cache elides the re-send and a discarded setting is lost, not delayed.
- [ ] 3.4 Note that `clearQueue()` also drops queued **reads** (`read()` goes through `queueCommand`, `bletransport.cpp:236-247`), including MMR verify read-backs. Confirm nothing depends on a read surviving a drain, or exempt reads.
- [ ] 3.5 Log the discard and the number discarded. Do not attempt to name *which* settings were dropped: `m_commandQueue` is `QQueue<std::function<void()>>` (`bletransport.h:126`) and entries carry no metadata.
- [ ] 3.6 Extend the drain to the terminal-write-failure case at both exhaustion sites (`bletransport.cpp:138` write timeout, `:734` `CharacteristicWriteError`) once 3.1-3.3 are in place.
- [ ] 3.7 Tests in `tst_bletransporterror.cpp`: a supersede drains the previous upload's writes; an urgent state write survives a drain; a drained MMR value is actually re-sent afterwards rather than elided.

## 4. Upload retry gating

- [ ] 4.1 Gate the ProfileManager retry (`src/controllers/profilemanager.cpp:239-245`) on `queueDrained` instead of the 1/2/4/8 s ladder. The signal exists (`bletransport.cpp:150`, `:743`, `:867`).
- [ ] 4.2 Add a bounded backstop so a queue that never drains cannot defer the retry indefinitely.
- [ ] 4.3 Test slots in the existing ProfileManager tests: a retry does not begin while the previous attempt's writes are pending; it proceeds once drained; the backstop releases it.

## 5. Queue depth observability

- [ ] 5.1 Record when the pending queue passes a depth threshold in `BleTransport::queueCommand()` (`bletransport.cpp:996`), which today has no cap and no warning. de1app warns at 20 (`de1_comms.tcl:49`).

## 6. Consecutive-failure detection (reporting only)

- [ ] 6.1 Count consecutive abandoned writes on the DE1 link at both exhaustion sites, resetting on any successful write and on disconnect. Set the bound at the low end of the observed gap: nine logs peak at 1, #1713 at 2, then 7 / 8 / 11 / 89. Note in the comment that the corpus figure is a proxy that overestimates, since the logs carry no success marker.
- [ ] 6.2 On the bound being passed, emit the log line. Per `docs/CLAUDE_MD/LOGGING.md` this is user-facing, so `INFO` or `WARN` — the connection views default to `minLevel INFO` and a `DEBUG` line would be invisible there.
- [ ] 6.3 Write the line to be self-explanatory: the link stopped accepting writes while still reporting connected, plus the remedy.
- [ ] 6.4 Add **no** teardown on this signal, and **no** `bleError`. Verify by inspection. See design.md Non-Goals for why both are out of scope.
- [ ] 6.5 Do **not** feed this into `evaluateBleWedge`: it gates on `!m_de1Connected` (`blemanager.cpp:356`) and a link in this condition is connected, so it could never fire.
- [ ] 6.6 Test slots: bound passed → recognised; an intervening success resets; a disconnect resets.

## 7. MMR split assurance

- [ ] 7.1 Make `0x803828` (`DE1::MMR::STEAM_FLOW`, `de1characteristics.h:136`) use one assurance level. It is written verified at `src/controllers/maincontroller.cpp:3623` and unverified at `:2722` and `:3562`.
- [ ] 7.2 Note that the verified site's own comment records on-device testing showing zero retries were needed — evidence that argues for levelling down rather than up. Decide deliberately and state the reason at the call site.
- [ ] 7.3 Do **not** route the other MMR call sites through `writeMMRVerified` in this change. An MMR read is itself a write — `sendMMRReadRequest()` writes 20 bytes to `a005` (`de1device.cpp:777-785`, props `0x1a`) — so read-back verification adds a second write plus a retry ladder that issues more `a005` writes, on the same contended link. 55 of the corpus's 384 exhaustions are `a005` read requests. `writeMMRVerified` also uses `force=true`, bypassing the #773 dedup cache (`de1device.cpp:1545-1552`).
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
