## Why

After connecting to the DE1, several one-shot MMR (Memory-Mapped Register) reads —
GHC status/info, CPU board model, machine model, firmware build number, heater
voltage, refill-kit status — are sent immediately alongside a batch of
BLE notification-enable (CCCD) descriptor writes that are fired without waiting
for any of them to complete. When a read's response notification races the still-
in-flight CCCD write for the same characteristic (`READ_FROM_MMR`), the response is
silently dropped: there is no retry, so the value never arrives for the rest of
that session. This was found while investigating why the on-screen Stop button
sometimes doesn't reflect an active GHC paddle — `DE1Device::isHeadless` (driven by
the GHC_INFO read) was staying at its permissive default because the read's
response never came back. A live debug-log capture confirmed zero GHC-status log
lines in an otherwise-normal session, and the machine's third firmware-info line
(which needs the identity reads) was similarly missing.

Following that root cause to its sibling DE1 controller project, reaprime, and
auditing the rest of Decenza's DE1 BLE traffic against it surfaced three more
timing/reliability gaps of the same kind — intermittent, hard to reproduce, and
already hit and fixed by reaprime with a cited real-world failure behind each one:

1. **Post-connect notification race** (the one above): reaprime fixed it with
   sequential/awaited subscriptions plus subscribe-before-write, timeout, and
   retry on every one-shot MMR read.
2. **No settle time between finishing a profile upload and starting espresso.**
   The DE1 firmware writes the shot descriptor to internal flash on the final
   frame/tail write and only clears its internal "download in progress" flag once
   that flash write completes; if a state=espresso request arrives first, the
   firmware aborts the shot to HeaterDown right after preinfusion. reaprime
   documents this against a specific bug report and inserts a 500ms guard before
   allowing the state change. Decenza's recipe-activation path fires
   `startEspresso()` right behind the profile upload with only the BLE command
   queue's normal 50ms pacing between them.
3. **`writeMMRVerified`'s read-back has no timeout.** It retries on a value
   mismatch, but if the read-back notification is simply dropped — the same
   failure mode as (1) — the pending verification sits forever with no retry and
   no failure logged.
4. **No detection of a "zombie" link**: GATT stays connected, writes still ACK,
   but push notifications (state, shot samples) have silently stopped. reaprime
   tears down and re-subscribes on a reconnect attempt specifically to guard
   against this. Decenza's wedge detector doesn't key off this signal, and
   `connectToDevice()` currently early-returns "already connected" — so a
   reconnect attempt against a zombied link would be silently skipped entirely.

All four share the same character: intermittent, timing-dependent, invisible in
normal testing, and already independently discovered and fixed in a project
talking to the identical DE1 BLE protocol — worth fixing together rather than
piecemeal.

## What Changes

- Make BLE notification subscription (`BleTransport::subscribeAll()`) wait for each
  CCCD descriptor write to actually complete before subscribing to the next
  characteristic, instead of firing all writes back-to-back with no completion
  tracking.
- Gate the post-connect one-shot MMR reads (GHC_INFO, CPU_BOARD_MODEL,
  MACHINE_MODEL, FIRMWARE_VERSION build number, HEATER_VOLTAGE, refill-kit status)
  so they are only issued after the `READ_FROM_MMR` notification subscription is
  confirmed enabled, closing the race window entirely rather than just narrowing it.
- Add a timeout + bounded retry for these one-shot MMR reads, mirroring the
  existing write-side `writeMMRVerified`/`scheduleMMRVerifyRead`/`retryMMRVerify`
  pattern, so a response dropped for any other reason (not just the subscription
  race) is recovered instead of silently lost for the session.
- Extend that same retry/timeout helper to `writeMMRVerified`'s read-back, so a
  dropped verification-read notification is retried instead of abandoned forever.
- Add a settle window after a profile upload's final frame/tail write completes,
  before a queued `startEspresso()` (or any state change) is allowed to proceed,
  so the app never races the DE1 firmware's internal flash-write / flag-clear.
- Detect a "zombie" DE1 link (connected, writes ACK, but expected periodic
  notifications have stopped arriving) and force a teardown + reconnect instead of
  silently doing nothing; remove/adjust the "already connected" early return in
  `connectToDevice()` so a reconnect attempt against a zombied link isn't skipped.
- Remove `DE1Device::requestGHCStatus()`, a dead function with no call sites — the
  actual GHC read is inlined in `sendInitialSettings()`; keep one call path, not two.

## Capabilities

### New Capabilities
- `de1-mmr-read-reliability`: One-shot MMR reads and write-verification read-backs
  reliably arrive or are retried, instead of silently depending on
  subscription/response timing that can race on some platforms.
- `de1-profile-upload-settle`: A profile upload is fully settled (including the
  DE1 firmware's internal flash write) before any subsequent state change such as
  starting espresso is allowed to proceed.
- `de1-connection-health`: A DE1 link that is nominally connected but has silently
  stopped delivering expected notifications is detected and recovered via
  teardown + reconnect, rather than left as an undetectable zombie.

### Modified Capabilities
(none — no existing spec covers post-connect MMR read sequencing/retry, profile
upload settle timing, or zombie-link detection)

## Impact

- `src/ble/bletransport.cpp` / `src/ble/bletransport.h`: `subscribeAll()`,
  `subscribe()`, `connectToDevice()` — CCCD completion tracking, and removing/
  adjusting the "already connected" early return for the zombie-link case.
- `src/ble/de1transport.h`: transport interface, if `subscribe()`'s signature needs
  to expose completion (e.g. callback/signal) to callers.
- `src/ble/de1device.cpp` / `src/ble/de1device.h`: `sendInitialSettings()`,
  `parseMMRResponse()`, `writeMMRVerified()`/`scheduleMMRVerifyRead()`/
  `retryMMRVerify()`, `uploadProfileAndStartEspresso()`/frame-upload completion
  path, removal of unused `requestGHCStatus()`; new retry/timeout bookkeeping for
  one-shot MMR reads (`m_isHeadless`, `m_cpuBoardModel`, `m_machineModel`,
  `m_firmwareBuildNumber`, `m_heaterVoltage`, `m_refillKitDetected`) and for the
  profile-upload settle state.
- `src/ble/blemanager.cpp`: wedge/link-health detection (`evaluateBleWedge` and
  the signals feeding it) — new zombie-link signal.
- `src/maincontroller.cpp`: recipe-activation → `startEspresso()` path
  (`recipeActivated`, `startSelectedRecipeShotWhenApplied`), to route through the
  new profile-upload settle gate.
- `tests/tst_de1device_headless.cpp` and any BLE-layer tests exercising connect
  sequencing, GHC detection, profile-upload-to-espresso timing, or reconnect
  behavior.
- No QML/UI changes — this proposal is scoped to BLE-layer reliability, not the
  Stop-button visibility question raised during the same investigation (tracked
  separately, pending a scope decision).
