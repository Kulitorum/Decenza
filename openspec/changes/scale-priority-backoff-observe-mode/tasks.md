## 1. Persistence (settings_hardware)

- [x] 1.1 Add `QString cpMode() const` / `void setCpMode(const QString&)` to `SettingsHardware`, persisting `connectionPriority/policyMode` (absent ⇒ treated as `enforce`); NOT build-scoped (the build-rehydrate path must not read or rewrite it).
- [x] 1.2 Narrow `clearConnectionPriorityLatch()` to remove only `connectionPriority/{latched,triggerKind,setTimeIso,buildCode}` instead of `m_settings.remove("connectionPriority")`, so `policyMode` survives a latch clear.
- [x] 1.3 Add a regression test asserting `policyMode` is still present after `clearConnectionPriorityLatch()`. (tst_scaleskiphighlatch::cpModeSurvivesLatchClear)

## 2. BLEManager — mode + observe-event ring

- [x] 2.1 Add `enum class BackoffMode { Enforce, Observe }` with string<->enum mapping (`"enforce"`/`"observe"`, unknown ⇒ `Enforce`).
- [x] 2.2 Add `backoffMode()` getter and `setBackoffMode(BackoffMode)` setter; load from `SettingsHardware::cpMode()` in `setSettings()` (separate from latch rehydrate, not build-scoped) and write through on set.
- [x] 2.3 Add a fixed-capacity (20) in-memory observe-event ring: each entry `{ QDateTime time, QString triggerKind, enum {WouldBackoff, Recovered} kind, double durationSec }`; add `recordObserveEvent(...)` and a read accessor (most-recent-first). In-memory only.
- [x] 2.4 Ensure `setBackoffMode(Observe)` does NOT clear/erase the latch (observe overrides at the transport, latch value preserved); switching to `Enforce` leaves the latch to be honored as before.

## 3. Detector — observe-aware non-latching path

- [x] 3.1 Add observe state to `BlePriorityDetector` (seeded at `armWindow()` from a passed-in bool, mirroring the existing skip-high seeding; keep the type Qt-free/clock-free).
- [x] 3.2 Route `onDe1Fault()` / `onScaleStall()` to a `wouldFire()` when armed-observe: returns true WITHOUT setting `m_skipHighPriority`/`m_backoffTriggered` and WITHOUT disarming; reset the cluster window (`m_de1FaultCount`/`m_windowStartMs`) so the next episode is detected cleanly (no fire-once).
- [x] 3.3 Expose a way to observe-log a DE1-fault-cluster window expiry without escalation (use the existing `nowMs - m_windowStartMs > kWindow` re-anchor branch) so the transport can log "cluster subsided".
- [x] 3.4 Keep `enforce` paths byte-identical (no behavior change when observe is false).

## 4. WeightProcessor — recovery signal

- [x] 4.1 Record the wall-clock at which `m_scaleFeedStale` is set in `checkScaleFeedStall()` (stall-start time).
- [x] 4.2 In `processWeight()`, at the exact point `m_scaleFeedStale` is cleared by a genuine sample, if a stall had been signalled this cycle emit a new `scaleFeedResumed(qint64 gapMs)` exactly once on the 1→0 edge; reset stall-start. No timer.
- [x] 4.3 Declare `void scaleFeedResumed(qint64 gapMs)` signal in `weightprocessor.h`; confirm SAW/flow/frame-exit code paths are untouched.

## 5. Transport wiring (qtscalebletransport)

- [x] 5.1 In `onControllerConnected()`, check `BLEManager::backoffMode()` first: if `Observe`, do NOT seed skip-high from the manager latch, always request HIGH, and `armWindow()` in observe; if `Enforce`, today's behavior unchanged.
- [x] 5.2 Add `logWouldBackoff(reason, triggerKind)`: WARN log clearly marked observe/no-action + `BLEManager::recordObserveEvent(WouldBackoff, kind, stallSec)`; do NOT latch/disconnect/reconnect.
- [x] 5.3 Gate `onDe1LinkFault()` / `onScaleFeedStalled()` to call `triggerScaleBackoff()` (enforce) vs `logWouldBackoff()` (observe) based on which detector path returned true.
- [x] 5.4 Add an observe handler for the resume signal: WARN "feed RESUMED after X.X s — recovered at HIGH" + `recordObserveEvent(Recovered, "scale-feed-stall", gapSec)`; and a log for the detector's cluster-subsided notification.

## 6. main.cpp signal wiring

- [x] 6.1 Connect `WeightProcessor::scaleFeedResumed` to the transport observe handler cross-thread with explicit `Qt::QueuedConnection`, mirroring the existing `scaleFeedStalled` → `onScaleFeedStalled` wiring (same pinning/threading comment style).

## 7. MCP tools (mcptools_devices)

- [x] 7.1 Add `devices_set_scale_priority_mode(mode: "enforce"|"observe", confirmed: bool)`: reject unknown `mode`; require `confirmed`; persist mode immediately; eventually-consistent response wording (mode persisted now; HIGH-forcing applies on next scale reconnect; current connection not torn down) — mirror `devices_reset_scale_priority` phrasing.
- [x] 7.2 Extend `devices_connection_status`: add `backoffMode` (`"enforce"`/`"observe"`) and `recentObserveEvents` (bounded, most-recent-first; each entry ISO-8601-with-offset `time`, human `triggerKind`, `kind` ∈ `wouldBackoff`/`recovered`, and `stallSec`/`gapSec`). Empty list when never observed.
- [x] 7.3 Verify all new MCP fields follow CLAUDE.md conventions (ISO-8601 + offset, units in field names, human-readable enum strings; no Unix timestamps).

## 8. Tests

- [x] 8.1 `bleprioritydetector` unit tests: observe → repeated `wouldFire` true across multiple episodes, never sets skip-high/backoff-triggered, stays armed; identical trigger point vs enforce on the same input sequence; cluster-window-expiry notification fires in observe.
- [x] 8.2 `tst_weightprocessor`: `scaleFeedResumed` emits exactly once on the stall→sample edge with the correct gap; not emitted without a preceding stall; SAW/flow/frame decisions byte-identical with the signal present vs absent.
- [x] 8.3 `settings_hardware` test: `policyMode` survives `clearConnectionPriorityLatch()` (task 1.3) and round-trips across a simulated restart; absent key ⇒ `enforce`.
- [x] 8.3a **(added from PR review)** `tst_scaleskiphighlatch`: `ObserveEvent` factories stamp time + clamp negative duration; `ObserveEventRing` is newest-first and bounded at capacity dropping the oldest (closes review GAP-1 — the ring was extracted header-inline specifically so this is unit-testable without linking blemanager.cpp). `tst_weightprocessor`: asserts the `scaleFeedStalled` `gapMs` value (closes review IMP-1).
- [~] 8.4 **Deferred (no harness; mostly covered by construction).** No `registerDeviceTools` unit harness exists (the existing `devices_reset_scale_priority` likewise has none); the tool is thin validation + a marshalled `setBackoffMode`. The ring ordering/bounding + factory clamp (review GAP-1) and mode persistence are now directly unit-tested (8.3/8.3a); the detector observe contract is unit-tested. **Honest residual (review GAP-2):** `setBackoffMode()`-does-not-clear-the-latch is *structurally* guaranteed (it only calls `setCpMode`, which writes only `policyMode`; no latch key is touched — and the inverse, clear-latch-preserves-mode, IS tested) but is not exercised through a live `BLEManager` instance, because that needs the heavy Qt-BLE link a `BLEManager` test target would pull. Accepted as structural + the §9.3 device check, not worth a dedicated heavy harness. End-to-end tool shape is verification 9.2.
- [x] 8.5 Build via Qt Creator MCP (0 errors, 0 warnings) and run the full Qt Test suite green. (Build 0/0; suite **2148 passed / 0 failed / 0 skipped / 0 with-warnings**.)

## 9. Verification

- [~] 9.1 **Deferred to maintainer device test.** The *detector* enforce path is byte-identical by construction (tst_scaleblepriority `enforceUnchangedWhenNotObserving` / `observeAndEnforceShareTriggerPoint`; `m_observe` defaults false, latch/disarm untouched). **Honest scope note (from PR review):** the *transport routing* in `onControllerConnected` (observe-vs-enforce branch) is genuinely new code, NOT byte-identical-by-construction — the enforce `else` branch is the verbatim prior code but the dispatch is new; it has no unit harness (needs a `QLowEnergyController`) so the live debug-log diff on hardware is the real check → maintainer.
- [~] 9.2 **Deferred to maintainer device test (the feature's whole point).** Requires a real scale-stall on hardware running this build: set `observe`, force a scale reconnect, induce/await a stall; confirm link stays HIGH, the `[observe] WOULD back off` + `feed RESUMED` WARN lines appear, no latch set, and `devices_connection_status` shows `backoffMode: observe` + the event list. Unit/integration coverage is green; only the live evidence-gather is hardware-bound.
- [~] 9.3 **Partially deferred.** Mode persistence + latch-clear-preserves-mode is unit-locked (tst_scaleskiphighlatch `cpModeDefaultsEmptyAndRoundTrips` / `cpModeSurvivesLatchClear`). The live `devices_reset_scale_priority`-leaves-mode-intact check on the running build is part of the maintainer device test.
