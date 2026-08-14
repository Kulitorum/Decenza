## Why

Decenza spends almost all of its BLE write-retry budget on writes that will never land, and
issues fresh work into a queue still draining doomed writes. Measured across 434 retry
cycles in 28 user-submitted debug logs (format-aware, session-scoped):

| highest retry reached | cycles | recovered |
|---|---|---|
| 1-4 | 37 | 37 |
| 5-9 | 17 | 17 |
| **10** | **380** | **1** |

Every write that was going to succeed did so by retry 9. Exactly one write in the entire
corpus succeeded on the tenth retry, while 380 cycles — 88% of all retry activity — ran the
full budget and failed. With `WRITE_TIMEOUT_MS = 5000`, `MAX_WRITE_RETRIES = 10` and
`WRITE_RETRY_DELAY_MS = 500` (`src/ble/bletransport.h:133-136`), a timing-out write occupies
**11 × 5 s + 10 × 0.5 s = 60.0 s** before being abandoned — measured dispatch-to-abandonment
in [#1691](https://github.com/Kulitorum/Decenza/issues/1691) at 60.06 s. That is longer than
the 60 s MMR keepalive period, so on that log the link is never idle between writes.

**Retries are issued into a queue that has not drained.** `clearQueue()` is called only from
`goToSleep()` and `clearCommandQueue()` (`src/ble/de1device.cpp:1228`, `:1259`).
`startProfileUploadTracking()` detects that a new upload supersedes an in-flight one
(`:1324-1326`) and does nothing to the transport queue, and ProfileManager's retry ladder
fires on a 1/2/4/8 s timer (`src/controllers/profilemanager.cpp:239-245`) irrespective of
whether the previous attempt's writes are still failing. The cascade is visible end-to-end in
[#1466](https://github.com/Kulitorum/Decenza/issues/1466): upload fails at 1086.185, the
retry re-uploads at 1088.182 while `retrying 7/10` from the previous attempt is still running
at 1088.164, fails again at 1098.169, and the controller drops the link at 1098.808.
`queueCommand()` (`src/ble/bletransport.cpp:996`) has no depth cap and no warning; de1app,
by comparison, warns when its queue passes 20 (`de1_comms.tcl:49`).

**A link can stop accepting writes without ever disconnecting, and nothing notices.** In 12
of the 14 distinct corpus logs carrying write exhaustions, the link does drop and the
existing reconnect ladder runs — that path is working. The exceptions are
[#1810](https://github.com/Kulitorum/Decenza/issues/1810), where **0 of 154** exhausted
writes were followed by a disconnect, and #1691. Counting consecutive write failures with a
reset on any success separates these cleanly: nine logs peak at 1, #1713 peaks at 2, and the
pathological cases sit at 7 (#1691), 8 (#1586), 11 (the SM-T503 session) and 89 (#1810). The
gap between 2 and 7 is empty.

**Why this is invisible to most users, and catastrophic for a few.** The rate at which
writes fail at all varies enormously by device and session:

| log | failed writes, as a share of writes issued |
|---|---|
| #1810 | **49.0%** |
| #1466 / c1469 (one SM-T503 session) | 1.9% / 1.3% |
| c1280, #1691, #1586, #1485 | 0.2 – 0.9% |
| #1424, #1176, c1300, #1713, c1309, #1238 | **0.04 – 0.15%** |

At 0.05% the current design never gets exercised: every observed recovery happens by retry 9,
so the budget almost always succeeds, and the rare exhaustion is followed by a disconnect and
a reconnect that works. **The design has no damping** — the cost of a failure is a fixed 60 s
and a retry is issued regardless of whether the queue drained — so it degrades non-linearly.
At 0.05% the chance of ten consecutive failures is negligible; at 49% it is near-certain, and
each one costs a minute. #1810 is not "the same problem, more": it is 25× the next-worst log.

This change removes the amplifier. It does not lower the underlying failure rate, so it is a
robustness fix rather than a cure — a device at 1-2% degrades gracefully instead of falling
off a cliff, while a device at 49% remains a poor experience.

**One machine setting is written at two different assurance levels.** `0x803828`
(`STEAM_FLOW`) goes through the verified path at `src/controllers/maincontroller.cpp:3623`
and the unverified path at `:2722` and `:3562`. Whether that setting sticks depends on which
code path last wrote it, which is not diagnosable from behaviour.

**The automatic remedy for a wedged stack cannot work on modern Android.** In #1810
(Android 14, SDK 34), all 100 adapter power-cycle attempts completed through the safety-timer
fallback that fires when the adapter never changed state, and all 100 reported success —
`BluetoothAdapter.disable()`/`enable()` are unavailable to a non-privileged app from API 33.

## What Changes

- **Cut the per-write retry budget from 10 to a flat 3-5**, and recalibrate the one consumer
  that was tuned against the old budget (below). No graduated or link-state-dependent budget:
  the census above shows a flat bound captures the benefit, and 380 of 434 cycles never
  recover at any budget.
- **Recalibrate the DE1-fault-cluster weighting.** `ble-connection-priority` currently
  specifies that "a 10-retry write-failed cascade counts as 2 faults, so a single cascade
  reaches the threshold on its own" against a ≥2/60 s fire threshold. That premise — that a
  cascade represents ~5 s of sustained write starvation — is false at a 3-retry budget, and
  the *rate* at which `de1LinkFault("write-failed")` fires rises correspondingly. In #1691 a
  single exhaustion already sets the app-run skip-HIGH latch, demoting every scale to
  BALANCED for the session, so getting this wrong has a real cost.
- **Discard the previous upload's pending writes when a new upload supersedes it** — that
  operation's own writes, not the queue. Both reference implementations supersede per-kind and
  producer-side and neither ever clears the queue to do it: de1app's
  `remove_matching_ble_queue_entries` (`de1_comms.tcl:1423`) is called at 14 sites, including
  `{^Espresso header:}` / `{^Espresso frame #}` at `:1487-88`; decaid never enqueues the
  superseded upload at all. Pair the discard with `m_lastMMRValues` invalidation, as
  `clearCommandQueue()` already does, or the dedup cache elides the re-send and the values are
  lost rather than delayed. **Not** discarding on terminal write failure — see below.
- **Make an upload retry unable to overlap its predecessor**, via an in-flight guard rather than
  the 1/2/4/8 s timer. Both references make this structurally impossible: in de1app the retry
  *is* the queue entry re-run in place (`de1_comms.tcl:167-190`), and in decaid the backoff timer
  is scheduled only after the previous attempt has thrown (`workflow_device_sync.dart:113-116`,
  `:177-184`). Decenza is the only one of the three where a retry can be issued against a queue
  still holding the last attempt, which is the #1466 stacking.
- **Record the pending-queue depth** when it passes a threshold.
- **Count consecutive write failures on the DE1 link and report a link that has stopped
  accepting writes**, resetting on any success and on disconnect. Detection and logging only.
- **Fix the split assurance on `0x803828`**, and require one assurance level per register.
- **BREAKING (internal remedy change): retire the adapter power-cycle on API 33+**, keeping
  the wedge detector and keeping the reconnect-ladder re-arm via an explicitly named caller.
- **Close two teardown gaps**: a scan-initiated scale connect stuck in `Connecting` is never
  torn down by the connection timeout, and a controller in `ConnectingState` is deleted
  without being disconnected first.

## Capabilities

### New Capabilities
- `ble-write-retry-policy`: What the app does when a BLE write fails — how long it retries,
  what happens to work queued behind the failure, how an upstream retry is paced against the
  queue, queue-depth observability, and recognising a link that has stopped accepting writes
  while still reporting itself connected.

### Modified Capabilities
- `ble-connection-priority`: The DE1-fault-cluster weighting is stated in terms of a 10-retry
  cascade; it must be re-derived when the retry budget changes, so the backoff fires on the
  same real-world condition rather than on a threshold the new budget reaches sooner.
- `device-reconnect`: Retires the adapter power-cycle where the platform makes it a no-op,
  requires an automatic remedy to report its true outcome, requires the reconnect-ladder
  re-arm to survive that retirement via a named path, and extends the existing
  "Direct-connect timeout aborts the controller" requirement to scan-initiated connects and
  to disconnecting before destroying a connecting controller.
- `de1-mmr-read-reliability`: One assurance level per MMR register.

## Impact

**Code**
- `src/ble/bletransport.{h,cpp}` — retry bound; selective discard; queue-depth
  reporting; consecutive-failure accounting at both exhaustion sites (`:138` write timeout and
  `:734` `CharacteristicWriteError`).
- `src/ble/de1device.cpp` — discard the previous upload's pending writes at the supersede point
  (`:1324-1326`), paired with `m_lastMMRValues` invalidation.
- `src/controllers/profilemanager.cpp` — in-flight guard on the upload retry.
- `src/ble/transport/qtscalebletransport.cpp` — fault-cluster recalibration (`:391-407`);
  disconnect a `ConnectingState` controller before deleting it (`:149-167`).
- `src/ble/blemanager.cpp` — gate `setAdapterPower()` on SDK; name the re-arm caller; widen the
  scale connection-timeout teardown (`:1826`).
- `src/controllers/maincontroller.cpp` — one assurance level for `0x803828`.

**Verification risk**
The retry, queue and upload-gating work is in cross-platform files the suite can reach, and
existing test files cover this area (`tst_bletransporterror.cpp`, `tst_blecontrollererror.cpp`,
`tst_de1device_mmrreads.cpp`). `blemanager.cpp` carries 9 `Q_OS_ANDROID` and 12 `Q_OS_IOS`
guards and `setAdapterPower()` is Android-only, so Linux/macOS CI never compiles it — that part
needs an Android CI build and is validated by removal.

**User-visible**
Fewer link drops after a failed profile upload; a machine setting that applies consistently;
a link that has stopped accepting commands says so in the log. Devices on API 33+ stop losing
10 seconds per recovery attempt to a call that does nothing.

**What this does not do for #1810, the issue that prompted it**
Stated plainly so nobody infers otherwise. The teardown gap fix addresses that reporter's
literal symptom — a scale connect held ~30 s in `Connecting` while every retry was rejected
as a duplicate. The retry bound and the adapter retirement help modestly. The supersede and
upload-gating work almost certainly does nothing for them: their two largest failure clusters
each had one dispatched profile upload in the preceding 120 s. At a 49% write-failure rate the
machine will still be unpleasant to use after this change, and the cause of that rate is not
identified here.

**Dropped after comparing against the reference implementations**
Two items from the first draft. **Draining on terminal write failure**: Decenza today continues
to the next command (`bletransport.cpp:148`, `:741`), de1app clears only on connect/disconnect,
and decaid's clear-on-timeout is about the *platform* operation queue, where a stuck entry blocks
every following operation — a constraint Decenza's app-level queue does not have. No quorum, and
no corpus evidence of harm. **A priority carve-out for urgent writes**: the hazard was
overstated. Every stop and sleep path clears *before* issuing the urgent write, and the clear
resets the in-flight flag, so the urgent write goes direct and is never queued at clear time.
Neither reference has any priority concept, so Decenza already protects a stop better than both.
The invariant stays in the spec, asserted by test rather than built.

**Docs**
Wiki manual: re-check connection-troubleshooting wording that tells users to toggle Bluetooth.
