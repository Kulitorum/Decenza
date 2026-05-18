> Two arms. **Arm 1 (epoch scoping)** is independent of #1219 and can land first. **Arm 3 (stall confirmation)** consumes `scaleFeedResumed` + the detector/transport observe plumbing from #1219 — implement stacked on `feat/scale-priority-backoff-observe-mode` (or rebase after it merges). Capable-hardware byte-identical is a hard invariant for both.

## 1. Arm 1 — epoch constant + persistence

- [x] 1.1 Add `kBleDetectionEpoch` (int, =1) to a deliberately-edited BLE constants header (NOT `versioncode.txt`/`versionCode`-derived; NOT CI-touched). Document next to it: bump only when a build changes BLE connection behavior or to re-classify every device once on that release — the deliberate global-reset lever replacing the accidental per-build reset.
- [x] 1.2 `settings_hardware.{h,cpp}`: add `int cpEpoch() const` reading `connectionPriority/detectionEpoch` (absent ⇒ sentinel meaning "legacy / no epoch", e.g. -1); extend `setConnectionPriorityLatch(...)` to also write `detectionEpoch`. Keep `cpBuildCode()` + the `buildCode` write as diagnostic-only. (`clearConnectionPriorityLatch()` four-key narrowing already landed in #1219 — confirm it also leaves `detectionEpoch` consistent, i.e. clear removes the epoch key too so a cleared record is fully fresh.)

## 2. Arm 1 — BLEManager gate + forward migration

- [x] 2.1 `BLEManager::setSettings()`: replace the `storedBuild == versionCode()` gate with `storedEpoch == kBleDetectionEpoch`. Same safety-valve mechanics otherwise (rehydrate vs `clearConnectionPriorityLatch()` + re-detect). Keep reading/logging `buildCode` as diagnostic.
- [x] 2.2 Legacy forward-migration: if `cpLatched()` and a `buildCode` is present but `cpEpoch()` is the "no epoch" sentinel ⇒ rehydrate the in-memory latch AND write `detectionEpoch = kBleDetectionEpoch` (one-time), and `qWarning`-log the migration. A fresh record whose epoch simply mismatches is discarded + re-detected (unchanged-from-today semantics, just epoch-keyed).
- [x] 2.3 `latchScaleSkipHighPriority()`: persist `detectionEpoch = kBleDetectionEpoch` alongside the existing latch metadata.
- [x] 2.4 Confirm capable hardware never reaches any of this (no latch ⇒ no rehydrate/migrate path); no behavior change when unlatched.

## 3. Arm 1 — MCP read

- [x] 3.1 `devices_connection_status`: add the current detection epoch (always) and, when latched, the diagnostic build code labeled as "last classified by build N" (explicitly not the gate). Follow CLAUDE.md MCP conventions; do not alter the existing ISO-8601 latch timestamp fields.

## 4. Arm 1 — tests

- [x] 4.1 `tst_scaleskiphighlatch` (header/QtCore-only pattern): epoch round-trips; same-epoch ⇒ rehydrate; different-epoch ⇒ discard; build code differs but epoch matches ⇒ still rehydrate.
- [x] 4.2 Legacy-record forward migration: latched + buildCode + no epoch key ⇒ rehydrated AND epoch stamped AND logged; re-read on a later same-epoch run ⇒ ordinary rehydrate (not re-migrated). Zero-extra-detection asserted at the settings/manager layer.
- [x] 4.3 `clearConnectionPriorityLatch()` leaves no stale epoch (cleared record is fully fresh: `cpLatched()` false, `cpEpoch()` sentinel).

## 5. Arm 3 — WeightProcessor suspected→confirmed

- [x] 5.1 Add `kScaleStallConfirmMs` (single tunable constant; provisional ~5–6 s, final value set by task 8.1 from #1219 field data). Keep `kScaleStaleMs` (2 s) as the suspected threshold.
- [x] 5.2 `checkScaleFeedStall()` (on the existing DE1 `setCurrentFrame` tick — NO timer): first crossing of `kScaleStaleMs` ⇒ suspected (emit existing `scaleFeedStalled(gapMs)` exactly as today; observe logging unchanged). When the gap further crosses `kScaleStallConfirmMs` and no recovery has occurred since the suspected edge ⇒ emit new `scaleFeedStallConfirmed(qint64 gapMs)` once.
- [x] 5.3 Recovery cancels: a `scaleFeedResumed` (the #1219 edge) between suspected and confirmed clears the pending-confirmation state so confirmed never fires for that episode; a later independent stall re-arms suspected cleanly. Reset pending-confirmation on the existing extraction/cycle resets too.
- [x] 5.4 Declare `scaleFeedStallConfirmed(qint64 gapMs)` in `weightprocessor.h`; confirm SAW/flow/frame-exit paths untouched (pure observation, like `scaleFeedStalled`/`scaleFeedResumed`).

## 6. Arm 3 — detector/transport act on confirmed only

- [x] 6.1 `scalebletransport.h`/`qtscalebletransport.{h,cpp}`: add `onScaleFeedStallConfirmed(qint64 gapMs)` (base no-op virtual, mirroring `onScaleFeedStalled`/`onScaleFeedResumed`). Enforce calls `m_priority.onScaleStall()` ONLY from the confirmed handler; the suspected `onScaleFeedStalled` handler no longer drives the backoff (it remains the observe/diagnostic log path + the "disarmed detector" log).
- [x] 6.2 Observe mode: log the full picture — suspected stall (existing), recovery (existing #1219), and confirmed = the real "would back off" (record the observe event on confirmed, not suspected). Update `logWouldBackoff` call site accordingly.
- [x] 6.3 `main.cpp`: wire `WeightProcessor::scaleFeedStallConfirmed` → `ScaleBleTransport::onScaleFeedStallConfirmed` cross-thread `Qt::QueuedConnection`, mirroring the `scaleFeedStalled`/`scaleFeedResumed` wiring + threading comment.
- [x] 6.4 `BlePriorityDetector` unchanged in logic (still single-shot on `onScaleStall()`); the *confirmation* lives upstream in WeightProcessor. Verify the DE1-fault path (`onDe1Fault`, ≥2/`kDe1FaultWindowMs`) is byte-identical.

## 7. Arm 3 — tests

- [x] 7.1 `tst_weightprocessor`: suspected fires at `kScaleStaleMs` with correct gap; confirmed fires only after `kScaleStallConfirmMs` with no recovery; `scaleFeedResumed` before the confirm threshold cancels (no confirmed); a later independent stall re-arms; SAW/flow/frame byte-identical with/without the new signal.
- [~] 7.2 **Covered by existing + construction.** Detector (`BlePriorityDetector`) is UNCHANGED here — confirmation lives upstream in WeightProcessor; existing `tst_scaleblepriority` DE1-fault/capable tests stay green and pin byte-identical. "Enforce latches only on confirmed" is asserted at the WeightProcessor layer (7.1: confirmed only fires post-persistence, recovery cancels) + the transport now calls `onScaleStall()` solely from `onScaleFeedStallConfirmed`; the transport routing seam itself is the same `QLowEnergyController`-bound seam #1219 deferred to the device check (8.3).
- [~] 7.3 **Deferred to device check (8.3), same seam as #1219.** The observe log/ring behavior (suspected+recovered→no would-backoff; suspected→confirmed→would-backoff) is transport-level and not unit-harnessable; the WeightProcessor signal ordering it derives from IS unit-tested (7.1).

## 8. Calibration + verification

- [~] 8.1 **Gate (deferred to maintainer, by design):** finalize `kScaleStallConfirmMs` from #1219 observe-mode field data (the recovered-gap distribution: pick a threshold cleanly above the transient self-recovery cluster and below genuine sustained stalls). Record the chosen value + rationale here; do not guess-ship.
- [x] 8.2 Build via Qt Creator MCP (0 errors / 0 warnings). Full suite green — **46 suites / 2155 passed / 0 failed** (run from the Qt-Creator-built binaries; Qt Creator's Autotest plugin model was stale post-rebase and under-reported, so verified via the binaries directly: new test fns confirmed compiled in via `-functions`, binary newer than source).
- [ ] 8.3 Maintainer device check (hardware-bound): on an epoch build, confirm an existing latched device migrates with no detection event; confirm a transient preheat blip logs suspected+recovered and does NOT latch; confirm a sustained dead feed confirms and latches; confirm `devices_connection_status` shows the epoch + diagnostic build code.
- [x] 8.4 Document the epoch-bump procedure (one-line constant change + changelog note) next to `kBleDetectionEpoch` and in the change’s design as the deliberate global-reset lever.
