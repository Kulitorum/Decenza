## 1. CCCD subscription completion tracking

- [x] 1.1 Connect `QLowEnergyService::descriptorWritten` in `BleTransport` (currently unconnected) and track each CCCD write to completion, mirroring the existing `m_writePending`/`onCharacteristicWritten` pattern for characteristic writes
- [x] 1.2 Sequence `subscribeAll()`'s 5 subscriptions so each is confirmed (or individually timed out) before the next proceeds, integrated into the existing command-queue mechanism rather than a parallel one-off sequencer
- [x] 1.3 Add a bounded timeout per subscription so one stuck CCCD write cannot block the connection indefinitely; log and proceed past a timed-out subscription
- [x] 1.4 Delay `emit connected()` in `onServiceStateChanged()` until all subscriptions have completed or individually timed out, instead of firing immediately after `subscribeAll()` is merely called

## 2. One-shot MMR read retry

- [x] 2.1 Add a pending-read tracking table (address → attempts/deadline), parallel to the existing `m_pendingMMRVerifies` used by `writeMMRVerified()`
- [x] 2.2 Wrap the GHC_INFO, CPU_BOARD_MODEL, MACHINE_MODEL, FIRMWARE_VERSION, HEATER_VOLTAGE, and refill-kit-status reads in `sendInitialSettings()`/`requestRefillKitStatus()` with the new retry/timeout helper
- [x] 2.3 In `parseMMRResponse()`, clear the matching pending-read entry when a response for that address arrives (additive — no early return, same style as the existing `m_pendingMMRVerifies` check)
- [x] 2.4 On retry exhaustion, log a warning identifying the specific read that failed and leave the associated value at its existing safe/permissive default (e.g. `isHeadless` stays `true`)
- [x] 2.5 Define named timeout/retry constants for the read side (resolve the design's open question: reuse `WRITE_TIMEOUT_MS`/`MAX_WRITE_RETRIES` vs. new constants matching reaprime's proven 4s/2-retries/300ms-settle values) — resolved as separate constants `MMR_READ_TIMEOUT_MS`=4000, `MMR_READ_MAX_RETRIES`=2

## 3. MMR write-verification read-back retry

- [x] 3.1 Extend `scheduleMMRVerifyRead()`/`retryMMRVerify()` to register a pending-read entry (reusing the table from 2.1) when a verification read is issued — `scheduleMMRVerifyRead()` now delegates to `issueMMRReadWithRetry()` directly
- [x] 3.2 On timeout with no response at all (distinct from today's mismatch-triggered retry), re-issue the verification read using the same bounded-retry helper from Task 2 — handled generically by `checkMMRReadTimeouts()`, no address-specific code needed
- [x] 3.3 On retry exhaustion for a verification read-back, log a warning identifying the address/reason instead of leaving `m_pendingMMRVerifies` populated forever with no signal — handled in `checkMMRReadTimeouts()`'s expire branch

## 4. Profile-upload-to-espresso settle window

- [x] 4.1 Identify/introduce the single completion point for a profile upload (header + all frames + tail acknowledged) that all state-change callers can key off, rather than gating per-caller — `finishProfileUpload(success)` stamps `m_lastProfileUploadCompleteMs`
- [x] 4.2 Add a settle window (starting from reaprime's proven 500ms `profileDownloadGuard` value) after that completion point, before a subsequent `requestState()` call is allowed to proceed — `startEspresso()` defers the remaining window via a single-shot timer
- [x] 4.3 Route the recipe-activation path (`MainController::recipeActivated`/`startSelectedRecipeShotWhenApplied` → `DE1Device::startEspresso()`) through this settle gate — both paths already call `startEspresso()`, which is where the gate now lives (no MainController change needed)
- [x] 4.4 Confirm `uploadProfileAndStartEspresso()` (if still in use anywhere) also respects the same gate rather than duplicating its own timing — it had no call sites and was removed; recipe activation uploads then calls `startEspresso()` separately, now gated

## 5. Zombie-link detection and recovery

- [x] 5.1 Track the timestamp of the last-received notification for a frequently-repeating DE1 characteristic (e.g. STATE_INFO) while connected — `m_notificationLiveness` (QElapsedTimer) restarted in `onCharacteristicChanged`, baselined on `connected()`
- [~] 5.2 Empirically confirm the DE1's normal STATE_INFO push interval across relevant machine phases, to set a liveness threshold that doesn't misfire during legitimate quiet periods — provisional `NOTIFICATION_STALE_MS`=30000 chosen conservatively; the app's WaterLevel/StateInfo logging is post-throttle so raw cadence still needs on-device instrumentation (deferred to manual verification 8.5)
- [x] 5.3 In `BleTransport::connectToDevice()`, replace the unconditional "already connected" early return with a liveness check: if the link is connected but notifications have stalled past the threshold, tear down (`disconnectFromDevice()` + recreate controller) and proceed with a fresh connect/subscribe sequence
- [x] 5.4 Feed the zombie-link signal into `BleManager::evaluateBleWedge` alongside the existing controller-error and write-timeout-exhaustion signals — emits `de1LinkFault("zombie-link")`, which already routes through `DE1Device` → `BLEManager::onDe1LinkFault` → `evaluateBleWedge`

## 6. Cleanup

- [x] 6.1 Remove the unused `DE1Device::requestGHCStatus()` declaration (`de1device.h`) and definition (`de1device.cpp`) — no call sites reference it

## 7. Tests

- [x] 7.1 Extend `tests/tst_de1device_headless.cpp` (or add a new test file) to simulate a dropped GHC_INFO response and verify the retry recovers `isHeadless` correctly — new `tests/tst_de1device_mmrreads.cpp`, `droppedGhcRecoversWhenRetrySucceeds`
- [x] 7.2 Add a test for the retries-exhausted path, confirming `isHeadless` (and the other affected fields) settle on their safe default rather than an unknown/stale state — `exhaustedGhcReadLeavesHeadlessDefault`
- [x] 7.3 Add a test for a dropped `writeMMRVerified` read-back, confirming it retries and eventually logs a warning if never recovered, without disturbing the existing mismatch-retry test coverage — `verifyReadbackExhaustionClearsVerify` (mismatch path in `tst_mmrwrite`/existing coverage untouched)
- [x] 7.4 Add a test confirming a queued `startEspresso()` (or equivalent state change) is held until the profile-upload settle window elapses, and proceeds normally afterward — `startEspressoDefersAfterProfileUpload` + `startEspressoFiresImmediatelyWithNoRecentUpload`
- [~] 7.5 Add a test simulating a zombie link (connected, writes ACK, stale notification timestamp) confirming `connectToDevice()` tears down and reconnects rather than no-oping — NOT added as a unit test: `BleTransport::connectToDevice()`/liveness live on the real `QLowEnergyController` path, which the MockTransport harness (it replaces `BleTransport` wholesale) cannot exercise. Deferred to manual verification 8.5.
- [~] 7.6 Add/extend a `BleTransport`-level test (if the test harness's transport abstraction allows it under `DECENZA_TESTING`) covering subscription-completion sequencing and the per-subscription timeout — same limitation: subscription sequencing is internal to `BleTransport` over a live `QLowEnergyService`, not reachable through the `DE1Transport` mock. Covered by manual verification 8.1/8.2 instead.
- [x] 7.7 Build and run the full suite via `mcp__qtcreator__build` / `mcp__qtcreator__run_tests` (never a shell `cmake`/`ctest` invocation) before marking this change ready for review — full suite: 94 passed, 0 failed, 0 warnings

## 8. Manual verification

> Group 8 is on-device verification for the maintainer — it needs a real DE1
> (and, for 8.2, an Android CI build). Left unchecked deliberately; the code is
> complete and the full local suite is green.

- [ ] 8.1 Launch the app locally and confirm via the debug log that a `"GHC status: ..."` line appears on every DE1 connect, not intermittently
- [ ] 8.2 Verify on a real Android device or an Android CI test build (per this repo's platform-code rule), since the original race is Android-GC/timing-sensitive and macOS testing alone would not catch a regression
- [ ] 8.3 Confirm the Steam/Espresso/etc. Stop-button GHC-visibility behavior (a separate, still-undecided UX question) is unaffected — this change only fixes read/timing reliability, not button visibility logic
- [ ] 8.4 Pull several shots via recipe activation and confirm none abort to HeaterDown right after preinfusion
- [ ] 8.5 Exercise a reconnect while deliberately simulating a stalled-notification link (if feasible) or at minimum confirm normal reconnect behavior is unaffected for a genuinely dropped/healthy link; also use this to tune `NOTIFICATION_STALE_MS` against measured raw push cadence (task 5.2)
