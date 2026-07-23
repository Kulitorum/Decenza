## Context

`BleTransport::onServiceStateChanged()` (src/ble/bletransport.cpp) currently does,
in one synchronous block, on reaching `RemoteServiceDiscovered`:

1. `setupService()` + `m_characteristicsReady = true`
2. `subscribeAll()` — fires 5 CCCD ("enable notifications") descriptor writes via
   direct `m_service->writeDescriptor()` calls, back-to-back, with **no** tracking
   of completion (`QLowEnergyService::descriptorWritten` is never connected
   anywhere in the file) — then queues 4 initial reads
3. `emit connected()` — immediately, not gated on any of the above actually finishing

`DE1Device::onTransportConnected()` treats `connected()` as "fully ready" (the
existing code comment there explicitly says RemoteServiceDiscovered is "a
Qt-level guarantee" that characteristics are populated) and immediately calls
`sendInitialSettings()`, which queues a further batch of MMR writes plus the
one-shot GHC_INFO / CPU_BOARD_MODEL / MACHINE_MODEL / FIRMWARE_VERSION /
HEATER_VOLTAGE / refill-kit reads — all over `READ_FROM_MMR`, the same
characteristic whose notification subscription may not have finished yet.

Live debug-log evidence: a real session showed the DE1 connect sequence complete
normally (state/shot/temperature data all flowing), but zero `"GHC status: ..."`
log lines and a missing third firmware-info line (which needs
`m_firmwareBuildNumber` to be non-zero) — i.e. the GHC_INFO and identity reads'
responses never arrived, while the *repeating* notifications (state, shot,
temperature) were unaffected, because a dropped repeating notification is masked
by the next one arriving moments later. One-shot reads have no such self-healing.

The sibling project reaprime (Flutter/Dart, same DE1 protocol) hit this exact
"post-connect GATT-busy window" and fixed it with (a) `await`-per-characteristic
subscription instead of fire-and-forget, (b) subscribing to the MMR response
stream *before* writing the read request, and (c) a 4s timeout + 2 retries with a
300ms settle delay on every one-shot MMR read.

A broader audit of Decenza's DE1 BLE traffic against reaprime, prompted by that
finding, turned up three more gaps of the same character:

- **Profile-upload-to-espresso settle.** reaprime's `_uploadProfileLocked` waits a
  documented 500ms (`ConnectionTimings.profileDownloadGuard`) after the final
  frame/tail write before allowing a state change, with a comment citing a
  specific firmware bug report: the DE1 writes the shot descriptor to internal
  flash inside its `APIView::write` handling for the final frame and tail, and
  only clears its internal `ProfileDownloadInProgress` flag when that flash write
  returns; a `state=espresso` request that arrives first makes the firmware abort
  to HeaterDown right after preinfusion. Decenza's `uploadProfileAndStartEspresso`
  queues `HEADER_WRITE`, all `FRAME_WRITE`s, a tank-preheat MMR write, then
  `REQUESTED_STATE = Espresso` back-to-back, separated only by the BLE command
  queue's normal 50ms pacing — and the live recipe-activation path
  (`maincontroller.cpp`'s `recipeActivated`/`startSelectedRecipeShotWhenApplied`)
  fires `startEspresso()` the instant profile upload completes, with the same gap.
- **`writeMMRVerified`'s read-back has no timeout.** `scheduleMMRVerifyRead()` /
  `retryMMRVerify()` only retry on a value *mismatch* inside `parseMMRResponse()`;
  there is no timer at all covering "no response arrived." A dropped
  verification-read notification — the identical failure mode as the GHC/identity
  reads above — leaves the pending entry in `m_pendingMMRVerifies` forever, with
  no retry and nothing logged.
- **No zombie-link detection.** reaprime's `connect()` explicitly probes the
  OS-level connection state and tears down a stale native link before
  re-subscribing when a reconnect is attempted while the transport still reports
  "connected," with a comment describing exactly this failure: a pure-push
  characteristic silently stopped delivering while solicited reads/writes kept
  succeeding, invisible to their zombie watchdog (which only counts GATT-op
  timeouts and own-advertisement probes). Decenza's `BleManager::evaluateBleWedge`
  keys only off controller errors and write-timeout exhaustion, and
  `BleTransport::connectToDevice()` early-returns "already connected" — so a
  zombied link with dead notifications but ACKing writes would never trigger
  recovery and would silently reject a reconnect attempt.

## Goals / Non-Goals

**Goals:**
- Eliminate the race between CCCD subscription and the post-connect one-shot MMR
  reads, by gating those reads on confirmed subscription rather than firing
  them concurrently.
- Give the one-shot MMR reads (GHC_INFO, CPU_BOARD_MODEL, MACHINE_MODEL,
  FIRMWARE_VERSION build number, HEATER_VOLTAGE, refill-kit status) a
  timeout+retry so a response dropped for *any* reason — not just this specific
  race — is recovered within the same connection instead of silently missing for
  the rest of the session.
- Keep the fix inside the existing BLE transport/command-queue abstractions so it
  reuses proven retry/timeout machinery rather than inventing a parallel one.
- Close the profile-upload-to-espresso race by settling on the DE1 firmware's
  internal flash-write/flag-clear timing before allowing a state change, matching
  reaprime's cited-bug-report-driven fix.
- Extend the same read-retry mechanism to `writeMMRVerified`'s read-back so a
  dropped verification response is recovered instead of abandoned forever.
- Detect a zombie DE1 link (connected, writes ACK, expected notifications absent)
  and force recovery via teardown + reconnect, instead of leaving it undetectable
  and rejecting the next reconnect attempt as a no-op.

**Non-Goals:**
- Not changing the scale BLE transport (`qtscalebletransport.cpp` and friends) —
  this is DE1-only.
- Not adding live/periodic re-detection of GHC presence mid-session (e.g. if a
  GHC is attached/detached without a reconnect) — no evidence the DE1 pushes
  unsolicited GHC_INFO changes; scope stays "reliable one-shot read at connect,"
  matching current and reaprime behavior.
- Not addressing the Stop-button GHC-visibility question raised during the same
  investigation — that is a separate QML/UX decision pending the user's scope
  call, not a BLE reliability bug, and is intentionally excluded here.
- Not a general overhaul of `BleTransport`'s write-queue architecture — additive
  only (a read-side counterpart to the existing write-verify pattern, plus a
  liveness check for zombie-link detection).
- Not changing profile upload's frame-level verification (the per-frame echo-byte
  check already in `uploadProfile`/`onProfileUploadWriteComplete`) — this proposal
  only adds a settle window *after* that verified upload completes, before the
  next state change.
- Not building a general connection-health dashboard or exposing zombie-link
  detection as a user-facing setting (per this project's preference for fewer
  settings) — it is an internal recovery mechanism.

## Decisions

### 1. Gate `connected()` on confirmed CCCD subscription, not fixed delay

Track each `subscribe()` call's CCCD descriptor write to completion by connecting
`QLowEnergyService::descriptorWritten` (currently unconnected) and counting
acks, the same way `m_writePending`/`onCharacteristicWritten` already tracks
characteristic writes. `subscribeAll()` becomes asynchronous: it fires the first
subscribe, and each `descriptorWritten` ack triggers the next, mirroring
`processCommandQueue()`'s existing dequeue-on-completion pattern. Only once all 5
are acked (or individually timed out — see Decision 3) does `BleTransport` emit
`connected()`.

This directly answers "are we not waiting long enough": the fix is not a longer
fixed wait, it's waiting for the actual completion event instead of no event at
all. A fixed delay would be exactly the kind of timer-as-guard CLAUDE.md's design
principles reject — fragile across platforms/BLE stacks and impossible to tune
correctly for a race whose timing is OS/GC/radio dependent.

**Alternative considered:** insert a fixed `QTimer::singleShot` delay between
`subscribeAll()` and `emit connected()`. Rejected — no duration is provably safe
across Windows/macOS/Linux/Android/iOS, and it's the exact anti-pattern this
codebase avoids elsewhere (event-based flags over timers-as-guards).

**Alternative considered:** leave `connected()` timing as-is and only fix the
reads (Decision 2/3) to be robust to arriving before subscription completes, e.g.
by polling `m_service` state. Rejected — reaprime's incident report shows the
underlying issue is that the *notification is never delivered at all* if the CCCD
write hadn't completed, not that it arrives before the app is "ready" for it. No
amount of read-side robustness recovers a notification the platform never sent to
the app; subscription completion must gate before the request is even issued.

### 2. Subscribe-before-write ordering for one-shot MMR reads

Once `READ_FROM_MMR` is confirmed subscribed (Decision 1), the one-shot reads
in `sendInitialSettings()` are already safe by construction — subscription for
that characteristic is a precondition of `connected()` firing at all, so by the
time these reads are queued, the response path is live. This matches reaprime's
"subscribe to the response stream before writing the request" ordering, just
enforced structurally (one subscription, confirmed once, before any reads use it)
rather than per-read.

### 3. Timeout + bounded retry for one-shot MMR reads

Add a small pending-read table (address → {expected reason, attempts, deadline}),
parallel to the existing `m_pendingMMRVerifies` used by `writeMMRVerified()`.
Each of the 6 one-shot reads (GHC_INFO, CPU_BOARD_MODEL, MACHINE_MODEL,
FIRMWARE_VERSION, HEATER_VOLTAGE, refill-kit) registers a pending entry when its
request is sent; `parseMMRResponse()` clears the matching entry when a response
for that address arrives (independent of the address-specific handling already
there — additive, no early return, same as the existing verify-read check at the
bottom of that function). A timeout timer retries the read (bounded attempts) if
no response clears the entry in time; after retries are exhausted, log a warning
and leave the field at its existing permissive/safe default (e.g. `isHeadless`
stays `true`) — consistent with the "restore permissive default" philosophy
already in `onTransportDisconnected()`.

Timeout/retry counts mirror reaprime's proven values (4s timeout, 2 retries, 300ms
settle) unless reuse of this codebase's existing `WRITE_TIMEOUT_MS`/
`MAX_WRITE_RETRIES` constants proves equivalent — see Open Questions.

**Alternative considered:** rely solely on Decision 1 (subscription gating) and
skip read-side retry entirely, on the theory that the race is now impossible.
Rejected — Decision 1 closes the *specific* race found, but BLE notifications can
still drop for other transient reasons (radio interference, Android GC pause
mid-delivery); reaprime's own comment indicates they kept retry even after fixing
subscription ordering. A one-shot read with no recovery path is a foot-gun this
proposal exists to remove.

### 4. Remove dead `requestGHCStatus()`

`DE1Device::requestGHCStatus()` is defined and declared but has no call sites —
the actual GHC read is inlined separately in `sendInitialSettings()`. Delete it
rather than wiring it in, to avoid two divergent code paths doing the same MMR
read.

### 5. Settle window after profile upload, before the next state change

Track profile-upload completion (the existing frame-ACK verification already
confirms the header and every frame landed) and, once the tail write is ACKed,
hold any queued `requestState()` call — specifically the recipe-activation path's
`startEspresso()` — for a fixed settle window before letting it proceed, mirroring
reaprime's `profileDownloadGuard` (500ms).

This is a case where a timer is the right tool, not a guard-timer to avoid: the
firmware's internal flash write and flag-clear are not observable over BLE at all
(no notification signals their completion — this is empirical behavior inferred
from a firmware bug report, not documented protocol), so there is no event to
key an event-based flag on. The existing `WRITE_TIMEOUT_MS` machinery detects an
*absent* ACK; this is different — the ACK for the tail write arrives normally,
and the race is with unsignaled firmware-internal work *after* that ACK. A fixed
settle window is the same category of exception the codebase already accepts for
UI auto-dismiss and periodic polling, applied here because no BLE-observable
alternative exists.

**Alternative considered:** infer completion from some other DE1 characteristic
(e.g. wait for a STATE_INFO change). Rejected — during profile upload the DE1
doesn't reliably transition state in a way that correlates with the internal
flash write finishing; reaprime's own fix uses a fixed delay for the same reason,
suggesting no better signal exists.

### 6. Read-retry helper extended to `writeMMRVerified`'s read-back

Reuse the pending-read table and timeout/retry logic from Decision 3 for the
verification read issued by `scheduleMMRVerifyRead()`, rather than building a
second parallel mechanism. The existing mismatch-triggered retry in
`retryMMRVerify()` stays as-is (it already handles the "got a response but it's
wrong" case); this decision only adds the "got no response at all" case, which
today has zero handling.

### 7. Zombie-link detection via notification liveness, checked on next reconnect attempt

Track the timestamp of the last-received notification for at least one
frequently-repeating characteristic (e.g. STATE_INFO, which the DE1 pushes
continuously while connected). This is a legitimate periodic/heartbeat use of a
timer per this project's own carve-out for polling — it is not a guard standing
in for an event-based flag, since "notifications have stopped" has no other
observable signal by definition.

When `connectToDevice()` is invoked while the transport already reports
connected, check this liveness timestamp: if it is older than an expected
threshold (well beyond the DE1's normal push interval), treat the link as a
zombie — tear it down (`disconnectFromDevice()` + recreate the controller) and
proceed with a fresh connect/subscribe/resubscribe sequence, instead of the
current unconditional early return. `BleManager::evaluateBleWedge` also gets this
liveness signal as an additional input alongside its existing controller-error
and write-timeout-exhaustion signals.

**Alternative considered:** rely solely on the existing wedge detector
(controller errors, write-timeout exhaustion). Rejected — reaprime's own comment
establishes that a zombie link acks writes normally, so neither signal fires;
liveness-of-notifications is a genuinely different, currently-absent signal.

## Risks / Trade-offs

- **[Risk]** Gating `connected()` on 5 CCCD acks adds latency before
  `requestState(Idle)`/`sendInitialSettings()` fire, versus today's immediate
  (but unsafe) emission. → **Mitigation:** each descriptor-write ack is expected
  in tens of milliseconds; bound each with a timeout (reuse or parallel the
  existing write-timeout constant) so a single stuck subscribe can't block
  connection indefinitely — log and proceed past it after timeout rather than
  hanging the whole connect.
- **[Risk]** `QLowEnergyService::descriptorWritten` reliability may itself vary by
  Qt Bluetooth backend/platform (the same class of issue that motivates this
  proposal for characteristic notifications). → **Mitigation:** the timeout in
  the mitigation above is the backstop either way; per this repo's own
  guidance, verify on a real Android CI test build before release, not just
  local macOS.
- **[Risk]** Read-side retry could add worst-case connect latency if the DE1
  firmware genuinely never answers (e.g. very old firmware without GHC_INFO
  support). → **Mitigation:** bounded attempts (matching reaprime's proven 3
  total attempts) and reads stay serialized through the existing 50ms-gated
  command queue, so worst case is additive, not multiplicative, and only
  manifests on genuine failure, not the common path.
- **[Risk]** Removing `requestGHCStatus()` could break something depending on its
  symbol existing. → **Mitigation:** already confirmed via grep — no call sites
  anywhere in `src/`.
- **[Risk]** The profile-upload settle window (Decision 5) delays the start of
  espresso by up to ~500ms after the user taps a recipe/pill — perceptible if
  tuned too high. → **Mitigation:** start from reaprime's proven 500ms value
  rather than guessing; this is a firmware-timing constant shared across any app
  talking to the same DE1 firmware, not something Decenza needs to independently
  discover.
- **[Risk]** The settle window only guards the recipe-activation
  `startEspresso()` path; other callers that upload a profile and change state
  (if any exist or are added later) could bypass it. → **Mitigation**: implement
  the guard at the point where profile-upload completion is signaled (not
  per-caller), so any current or future caller waiting on that completion signal
  is covered uniformly.
- **[Risk]** Zombie-link liveness detection (Decision 7) could misfire during a
  legitimate lull (e.g. DE1 asleep/idle with genuinely infrequent pushes) and
  force an unnecessary reconnect. → **Mitigation:** set the liveness threshold
  well above the DE1's normal push interval during connected/active states, and
  scope the check to when a reconnect is actually being attempted (i.e. this
  only matters when something already suspects the link, not as a background
  poll that could fire spuriously during normal idle operation).
- **[Risk]** Tearing down and recreating the controller for a suspected zombie
  link is itself a disruptive action if the liveness signal is wrong.
  → **Mitigation:** only triggered at the moment a reconnect is already being
  attempted (i.e. something already believes the link is down or is required),
  so a false positive costs one extra reconnect cycle, not a spurious
  disconnection of an otherwise-healthy, in-use link.

## Migration Plan

Pure BLE-layer behavior fix; no data migration, no new user-facing setting (per
this project's preference for fewer settings over new flags). Ships in a normal
release. Rollback is a plain revert — no persisted state format changes.

Verification: extend `tests/tst_de1device_headless.cpp` and add coverage for a
simulated dropped GHC_INFO/identity response to confirm retry fires and the
system still reaches a safe (permissive) state after exhausting retries. Add
coverage for a simulated dropped `writeMMRVerified` read-back, for the
profile-upload settle window (confirming `startEspresso()` doesn't proceed
before it elapses), and for zombie-link detection triggering teardown+reconnect
on a stale-but-connected link. Manual verification on a real Android device per
this repo's standing rule for platform/BLE changes, watching for the
`"GHC status: ..."` log line appearing on every connect (not intermittently) and
confirming shots no longer end abruptly right after preinfusion.

## Open Questions

- Reuse this codebase's existing `WRITE_TIMEOUT_MS`/`MAX_WRITE_RETRIES` constants
  for the new read-side timeout/retry, or introduce separate named constants
  matching reaprime's proven 4s/2-retries/300ms-settle values? Leaning toward the
  latter (read and write failure modes aren't guaranteed equivalent), to be
  confirmed during implementation.
- Should CCCD subscription tracking be integrated into the existing
  `m_commandQueue` (treating descriptor writes as first-class queued commands)
  or handled by a small dedicated sequencer local to `subscribeAll()`? Leaning
  toward integrating into the existing queue for consistency and to reuse its
  retry/timeout machinery, to be confirmed during implementation.
- Match reaprime's 500ms profile-upload settle constant exactly, or verify/tune
  it against Decenza's own BLE timing characteristics first? Leaning toward
  starting with reaprime's value (it's a firmware-timing constant, not an
  app-specific one) and only adjusting if testing shows it's insufficient.
- What liveness threshold (Decision 7) correctly distinguishes "DE1 legitimately
  quiet" from "zombie link"? Needs empirical confirmation of the DE1's normal
  STATE_INFO push interval across machine phases (idle vs. active) during
  implementation.
