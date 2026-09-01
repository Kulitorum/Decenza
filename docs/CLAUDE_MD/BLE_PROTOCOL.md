## BLE Protocol Notes

- DE1 Service: `0000A000-...`
- One process-wide GATT queue across every peripheral — see "The shared GATT queue" below
- Shot samples at ~5Hz during extraction
- Profile upload: header (5 bytes) + frames (8 bytes each) + extension frames (8 bytes, frame number + 32 for limiters) + tail frame (8 bytes)
- USB charger control: MMR address `0x803854` (1=on, 0=off)
- DE1 has 10-minute timeout that auto-enables charger; must resend command every 60s

### The shared GATT queue

**Qt serializes GATT work per CONTROLLER, not per adapter.** `readWriteQueue` and
`pendingJob` are instance fields of `QtBluetoothLE`, one instance per
`QLowEnergyController` (`QtBluetoothLE.java:949-950`), and the darwin backend has no
cross-peripheral queue either. Nothing in the framework orders one peripheral's
operations against another's, on any platform — so the cross-device guarantee is the
app's to supply. `BleGattQueue` (`src/ble/blegattqueue.h`) is that: one queue, one
operation in flight, every peripheral.

That is issue #1819. A scale's connect and characteristic discovery ran across the
DE1's notification-enable descriptor writes; all three were rejected with
`DescriptorWriteError`, and the app reported the machine CONNECTED with `STATE_INFO`,
`SHOT_SAMPLE` and `WATER_LEVELS` never enabled — no chart, no shot detection, no
stop-at-weight. de1app has one queue for the DE1 and the scale together
(`::de1(cmdstack)`, a single `::de1(wrote)` flag), which makes the state unreachable
rather than handling the error better.

decaid is a **counter-example, not a precedent** — this doc previously cited it as
one. It sets `UniversalBle.queueType = QueueType.perDevice`
(`universal_ble_discovery_service.dart:403`), deliberately serializing per device and
NOT across devices, so it does not prevent the #1819 interleaving. What it does do is
fail the connect: a failed subscribe throws out of `_bleConnect()`, the whole attempt
is abandoned and the reconnect ladder re-runs it against a fresh GATT. That half is
worth copying; the queue shape is not.

**Scale and refractometer transports had no in-flight concept at all** before this.
`QtScaleBleTransport::writeCharacteristic()` validated the link and called straight
through — no flag, no completion matching, no retry — and the same for discovery,
notify-enable and read. The DE1 side knew an operation was outstanding and the scale
side did not, so it had nothing to yield. Both now submit through
`ScaleBleTransport`'s shared plumbing.

**Policy travels with the operation, not with the queue.** DE1 operations carry the
retry budget below; everything else defaults to zero retries, which is exactly what
the scale and refractometer transports did before they were queued. Giving them the
DE1's budget would be actively harmful: a dead scale link would hold the shared slot
through the DE1's ~32 s worst-case retry sequence, starving the machine that budget
exists to protect.

**The queue owns no timer that decides an operation finished.** Dispatch is driven by
the operation's terminal outcome. Each transport owns one per-operation clock as the
outer bound for an operation the platform never answers at all — 5 s for reads,
writes and CCCD writes, 20 s for discovery (characteristic discovery took 6.0 s in the
#1819 capture, so the write budget would be a guarantee of failure rather than a
bound). `SUBSCRIBE_TIMEOUT_MS` was deleted rather than tuned: at 3000 ms it duplicated
and raced Qt's own `RUNNABLE_TIMEOUT` of 3000 (`QtBluetoothLE.java:69`) on the same
operation, and turned a `DescriptorWriteError` that arrived at +45 ms into a 3 s
stall, three times in one connect.

**Two platform facts the queue has to accommodate:**

- A write WITHOUT response completes at issue, because nobody acknowledges one. Qt's
  darwin backend hands it to CoreBluetooth and calls `performNextRequest` immediately,
  emitting no `characteristicWritten` (`btcentralmanager.mm:815-818`).
- CoreBluetooth delivers a read response and an unsolicited notification through one
  callback with nothing to distinguish them, so a notification can release an
  outstanding read's slot early. Harmless — the response still arrives. The Qt
  transport has separate signals and keeps them separate.

**Connect is deliberately NOT queued.** A connect is bounded only by
`CONNECT_WATCHDOG_MS` (35 s), so a wedged one would hold the shared slot for 35 s and
starve every other device — a worse failure than the one being fixed — and unlike a
GATT operation it is radio-scheduler work the host stack already interleaves.

### Recovering a DE1 link that has gone silent

A DE1 link can stop working while `QLowEnergyController` still reports it connected. Measured
twice in one user log (`debug-2.log`, builds 3571/3572): the app held an abandoned-write fault
**22.5 s before** Qt reported the controller `Unconnected`, and spent all 22.5 s discarding every
command sent to the machine — a stop press or a stop-at-weight stop in that window does nothing.

`m_notificationLiveness` (restarted by any inbound traffic — notifications and read responses alike, since `onCharacteristicRead()` delivers through `onCharacteristicChanged()`) always tracked this, but it was read in
exactly one place: `connectToDevice()`, to turn an incoming reconnect into a teardown. Nothing
calls `connectToDevice()` while the link claims to be connected, so the check never ran in the case
it exists for.

`BleTransport::evaluateLinkLiveness()` now runs it when a write is **abandoned** — the moment the
app first has hard evidence the link is not working — and tears the link down when the DE1 has
also been sending nothing. The teardown emits `disconnected()` and stops there; the existing
reconnect ladder in `main.cpp` owns the reconnect, its backoff and its attempt budget. Recovery
lands around 10 s instead of 22.5 s.

**Two signals have to agree, and silence alone is not enough.** An abandoned write on its own is
not conclusive (writes fail transiently); silence on its own is only suspicious against a cadence
nobody has measured. Together they say the link is carrying nothing in either direction.

| | Value | Role |
|---|---|---|
| `NOTIFICATION_STALE_CORROBORATED_MS` | 5 s | Silence required alongside an abandoned write. The only thing this transport acts on by itself. |
| `NOTIFICATION_STALE_MS` | 30 s | Unchanged, and used only by `connectToDevice()`'s zombie check. |

Both are **provisional** — the DE1's true minimum push cadence needs on-device measurement
(`harden-de1-ble-reliability` tasks 5.2 / 8.5); the app's own inbound logging is de-duplicated and
cannot supply it.

A silence-alone teardown was written and removed. It could not be justified from anything measured:
it would have been evaluated on every 60 s keepalive — roughly 5,850 times across the 97.6 h of the
log this change came from — betting each time on that unmeasured cadence, and it caught neither of
the two real episodes that the corroborated path does not already catch. Both of them began with an
abandoned write.

**There is no mid-operation guard, and that is deliberate.** One was written and removed: it
deferred the teardown while the machine was in an operating phase and writes still succeeded, on
the reasoning that a stop could still be delivered over such a link. It could never release — the
busy state came from `MachineState::phaseChanged`, which is driven by the very notifications whose
absence is being measured, so once the link went quiet mid-operation the phase froze at its last
value and the deferral held forever. Any future guard here needs an input that does not travel over
the link being judged.

The teardown also raises **no** `de1LinkFault`. That signal is wired unconditionally to the scale's
dual-HIGH priority detector, whose latch persists across restarts and whose own comment marks it
KNOWN OVER-EAGER — so a new fault kind sourced from an unmeasured silence threshold could
permanently demote every scale to BALANCED. The teardown's own `disconnected()` already drives the
reconnect.

The decision itself is `shouldRecoverAfterAbandonedWrite()` — pure, no link state — because `isConnected()`
needs a real controller and cannot be faked in a test. `tests/tst_bletransporterror.cpp` drives it,
including the assertion that the silence threshold stays below the platform's own notice.

Scale and refractometer paths are untouched by this. The scale has its own weight-sample stall
detection (`WeightProcessor`, `kScaleStaleMs`/`kScaleStallConfirmMs`); the refractometers
deliberately have none and must not gain it — an R2 is active only in short bursts on a page that
is already looking for it, so "no inbound data" is its normal idle state and a silence detector
would fire on correct behaviour.

### BLE Write Retry & Timeout (like de1app)

BLE writes can fail or hang. The implementation includes retry logic similar to de1app:

**Mechanism:**
- Each operation starts a 5-second clock (`WRITE_TIMEOUT_MS`)
- On error or no answer: retry up to 5 times with 500 ms delay (`MAX_WRITE_RETRIES`,
  `WRITE_RETRY_DELAY_MS` — the budget carries a 60-line derivation from a 26-log
  corpus in `bletransport.h`; read it before changing either)
- After max retries: `writeAbandoned` + `de1LinkFault("write-failed")`, and the queue
  moves on
- Queue is cleared when any flowing operation starts (espresso, steam, hot water, flush)

**Error logging (captured in shot debug log):**
```
[Bluetooth][GattQueue] retry 1/5 for write 0000a00f
Write FAILED after 5 retries (uuid=0000a00f, 8 bytes)
```

Older captures show `Write timeout, retrying 1/10 (uuid=...)` — retries were counted
and logged by the transport before the shared queue, and the budget was 10 before it
was 5. Grep for the corpus's wording when re-deriving from those logs, not this one.

**Key UUIDs:**
- `0000a001` = Version
- `0000a002` = RequestedState
- `0000a005` = ReadFromMMR
- `0000a006` = WriteToMMR
- `0000a009` = FWMapRequest (firmware update)
- `0000a00b` = ShotSettings (steam, hot water, flush settings)
- `0000a00d` = ShotSample (real-time shot data ~5Hz)
- `0000a00e` = StateInfo
- `0000a00f` = HeaderWrite (profile header)
- `0000a010` = FrameWrite (profile frames)
- `0000a011` = WaterLevels
- `0000a012` = Calibration

**Comparison to de1app:**
- de1app uses a soft 1-second fallback timer (just retries the queue)
- de1app has a `vital` flag for commands that must retry, unbounded at 500 ms — its own
  source says that "kind of kills the BLE abilities of the app" when the command never
  succeeds, and it pins the command at the head of a stalled queue. What actually
  protects de1app is that it issues its notification enables three separate times
  during connection setup: redundancy, not error handling.
- Ours: a 5-second per-operation clock, and every DE1 operation retries up to 5 times

### Connection-Failure Handling

Qt's `QLowEnergyController::disconnected()` signal only fires on a Connected→Disconnected transition — it is **not** emitted when a connection attempt fails part-way (Connecting→Unconnected without ever reaching Connected). To avoid leaving `DE1Device::m_connecting` stuck at `true` forever after a failed retry (which would block all subsequent reconnect attempts and the `de1Discovered` auto-connect path), `BleTransport::setupController()` watches the controller's `stateChanged` signal and synthesizes a `disconnected()` signal when the state reaches `UnconnectedState` without a preceding native `disconnected()`. A flag (`m_disconnectedEmittedForAttempt`) prevents double-emission and is reset to `false` at every point where a fresh BLE-level `QLowEnergyController::connectToDevice()` is about to fire: the outer `BleTransport::connectToDevice()`, the internal service-discovery retry timer, and (defensively) at the tail of `BleTransport::disconnect()`.

Symptom if this is broken: DE1 reboot drops BLE, app attempts one reconnect, the attempt fails, then app stays silent forever until restarted. The Scan Devices button also appears dead because the `de1Discovered` handler's `!isConnecting()` guard never clears.

### Connection Priority: A Latched Device Stays BALANCED Forever

A device that fails dual-HIGH detection is latched to BALANCED on both BLE links, and
**that latch is meant to be permanent**. It is not re-tested, it does not expire, and
nothing in the app re-arms it. The only thing that clears one is a deliberate
`kBleDetectionEpoch` bump in `blemanager.h`, which re-classifies every device once.

**Why it never re-tests:** the upside of HIGH connection priority is very small for
almost everyone, and the downside of finding out again is re-inflicting the fault the
latch was set to avoid — broken scale discovery and a ~70 s DE1 GATT collapse before
the detector can re-latch. Permanently BALANCED is the cheap side of that trade.

**Reading it in a log**, the startup line looks like a warning and is not:

```
[Scale][ConnectionPriority] Loaded persisted dual-HIGH-incapable classification
(epoch 1, build 3391 [diagnostic], trigger=de1-fault-cluster) — BOTH BLE links will
start at BALANCED this run (no detection window)
```

The build code is **provenance, not staleness**: it records which build first
classified the device, and it does not gate rehydration. A record set on build 3391
still loading on build 3576 is the design working, not a stale classification — so do
not report it as one, and do not propose an expiry, a re-detect, or an epoch bump to
"check whether the device got better". Same for the per-connect
`skipping HIGH (dual-HIGH-incapable latch set, trigger=…) — link stays at BALANCED`
lines that follow.

Mechanism (epoch scoping, legacy migration, the SDK<30 seed, observe mode) is
specified in `openspec/specs/ble-connection-priority/spec.md`.

### Profile Upload Frame-ACK Verification

`DE1Device::uploadProfile()` and `uploadProfileAndStartEspresso()` don't just count write completions — they verify that each `FRAME_WRITE` ACK's leading byte (the `FrameToWrite` field) matches the sequence we queued, in order. Modeled on de1app's `confirm_de1_send_shot_frames_worked` in `de1_comms.tcl`.

**Why:** Counting `characteristicWritten` signals alone can falsely report success when frames are silently dropped, reordered, or a stale profile remains loaded on the DE1 (e.g., if the original upload was never re-sent after a connection hiccup). Verifying the frame-number sequence surfaces these cases as real failures instead of mysterious early shot endings.

**Tracked state (per in-flight upload):**
- `m_uploadProfileTitle` — echoed into success/failure logs so operators can tell which profile the verdict refers to
- `m_uploadExpectedFrameBytes` — leading byte of every frame we queued (regular: `0..N-1`, extension: `i+32`, tail: `N`)
- `m_uploadSeenFrameBytes` — leading byte captured from each `FRAME_WRITE` ACK
- `m_uploadHeaderAcked` / `m_uploadEspressoStartAcked` — one-shot flags
- `m_uploadExpectEspressoStart` — set true by `uploadProfileAndStartEspresso()` so the tracker also waits for the `REQUESTED_STATE(Espresso)` ACK before calling the upload complete
- `m_uploadConnection` — stored `QMetaObject::Connection` for the `writeComplete` listener so `finishProfileUpload()` can disconnect it deterministically
- `m_uploadTimeoutTimer` — 10-second single-shot timer that surfaces stuck uploads as failures instead of hanging forever

**Failure paths all emit `profileUploaded(false)` with a `qWarning()` whose reason text matches exactly what appears in the log:**
- `frame sequence mismatch (expected vs seen byte list printed in hex)`
- `timeout waiting for write ACKs`
- `BLE disconnect during upload`
- `command queue cleared during upload`
- `superseded by a new upload`

**Log format** (both paths emit via `.noquote()`, so the profile title appears without surrounding quotes; the failure message's own inline `"%3"` quoting of the title is preserved because it's part of the reason string):
```
DE1Device: profile upload verified — 5 frame(s) ACKed in order for profile D-Flow / Q
DE1Device: profile upload FAILED — frame sequence mismatch (expected [0x00, 0x01, 0x02, 0x22, 0x03], got [0x00, 0x01, 0x02]). Profile "D-Flow / Q" was likely NOT correctly loaded on the DE1.
```

Regression coverage lives in `tests/tst_profileupload.cpp` (uses `MockTransport::ackAllWritesInOrder()` to simulate ACK sequences).

### Shot Debug Logging

`ShotDebugLogger` captures all `qDebug()`/`qWarning()` messages during shots:
- Installs Qt message handler when shot starts
- Stores captured log in `debug_log` column of shot history
- Users can view/export via shot history web interface
- BLE errors are automatically captured (use `qWarning()` for errors)

## DiFluid R2 Refractometer

Driver: `src/ble/refractometers/difluidr2.{h,cpp}`. Framing is `DF DF <func> <cmd> <datalen> <data…> <checksum>`, checksum = additive sum of all preceding bytes mod 256. Service `0x00FF`, characteristic `0xAA01`.

Reference: [DiFluid's `protocolR2.md`](https://github.com/DiFluid/difluid-sdk-demo/blob/master/docs/protocolR2.md). A second implementation worth consulting is [Beanconqueror's R2 driver](https://github.com/graphefruit/Beanconqueror/tree/master/src/classes/devices) — where the two agree, behaviour is settled.

### What the docs do not tell you

All of the following was read off a physical R2 (model `DFT-R102`, firmware `V230`). None of it is stated in DiFluid's documentation or Beanconqueror's enum, though the loop test gets one oblique mention (quoted below). Do not "correct" these back to what the spec implies.

- **`Cmd 3` is a loop test.** DiFluid's action table stops at `Cmd 2`. `Cmd 3` re-measures one sample every ~3 s until the reading stops moving, then ends with status 9. It is what the R2 escalates to whenever the prism is not thermally settled — refractive index is temperature-dependent, so the device refuses to answer while the temperature is still moving. The doc's only hint is the line "The respond of loop test when temperature is not stable".
- **A physical-button read uses `Cmd 0`** — byte-for-byte identical to an app-requested single test. This was an open question for a long time and is now settled.
- **Loop or not is about prism stability, not about who asked.** Auto Test always loops because it fires the moment the sample is loaded (the worst thermal moment); a manual read on a settled prism completes in ~3.5 s with no loop, and a manual read on a fresh sample loops just the same.
- **In the averaged-result packet (pack 3), `Data3-6` is NOT the refractive index.** It occupies the same offsets as the RI in pack 2, but on a run whose per-test RI read 1.34689 this field read 782332 — 7.82332% at the x100000 scale, i.e. a concentration, not an RI (which is ~1.3). It is the running average at higher precision, and the value quoted here was captured mid-run, so it does not match that run's final 7.83%. Logging it as an RI puts a wrong number in the record.

### Which packet is the reading

The Func-3 response carries the action it belongs to, and that decides which packet is the answer:

| Action | Per-test result (pack 2) | Averaged result (pack 3) | Ends on |
|---|---|---|---|
| `Cmd 0` single test | the reading, terminal | — | its own result |
| `Cmd 1` average test | one test of N, **not** a reading | the reading, delivered as it converges | status 6 |
| `Cmd 3` loop test | the reading, delivered each pass | — | status 9 |
| anything else | treated as `Cmd 0` | | |

**Unrecognised action codes fall back to single-test handling deliberately.** An exhaustive dispatch would have taken the `Cmd 3` path silent before we knew it existed. The worst case of the fallback is "no better than before"; the worst case of an exhaustive table is a working path that stops reporting.

### Measurement watchdog

`m_measurementTimer` is a **liveness** watchdog, not a bound on how long a measurement may take, and it is the documented exception to the project's no-timers-as-guards rule (a silent device emits no event, so no event-based mechanism can see it). It is restarted by any packet indicating progress, including status 10, which the R2 emits precisely because an individual test is running long.

This is not theoretical: a measured 3-test averaged run took **22.0 s**. Armed once at request time, the old fixed 15 s deadline would have aborted it ~7 s before the result arrived.

### Averaging is driver-level only

`requestAveragedMeasurement()` is implemented and hardware-verified; `setDeviceTestCount()` is spec-only and has never been exercised against a device. **Nothing calls either** and the device's own test count stays at 1. Three measured runs put single-reading scatter at σ ≈ 0.011% TDS; averaging three reduces that to ≈ 0.007%, and the ~0.005% gained is smaller than the 0.01% step the device reports in — so it cannot be represented in the answer, and it is an order of magnitude under sample-prep variance. The cost was 12–22 s against ~3.5 s. Keep them for coverage; do not wire them to a button without new evidence.

## Battery Management

### Smart Charging (BatteryManager)
- **Off**: Charger always on (no control)
- **On** (default): Maintains 55-65% charge
- **Night**: Maintains 90-95% charge
- Commands sent every 60 seconds with `force=true` to overcome DE1 timeout

## Steam Heater Control

### Settings
- **`keepSteamHeaterOn`**: When true, keeps steam heater warm during Idle for faster steaming
- **`steamDisabled`**: Completely disables steam (sends 0°C)
- **`steamTemperature`**: Target steam temperature (typically 140-160°C)

### Key Functions (MainController)
- **`applySteamSettings()`**: Smart function that checks phase and settings:
  - If `steamDisabled` → sends 0°C
  - If phase is Ready → always sends steam temp (machine heating, steam should be available)
  - If `keepSteamHeaterOn=false` → sends 0°C (turn off in Idle)
  - Otherwise → sends configured steam temp
- **`startSteamHeating()`**: Always sends steam temp (ignores `keepSteamHeaterOn`) - use when user wants to steam
- **`turnOffSteamHeater()`**: Sends 0°C to turn off heater

### Behavior by Phase
| Phase | keepSteamHeaterOn=true | keepSteamHeaterOn=false |
|-------|------------------------|-------------------------|
| Startup/Idle | Sends steam temp, periodic refresh | Sends 0°C |
| Ready | Sends steam temp | Sends steam temp (for GHC) |
| Steaming | Sends steam temp | Sends steam temp |
| After Steaming | Keeps heater warm | Turns off heater |

### SteamPage Flow
1. **Page opens**: Calls `startSteamHeating()` to force heater on
2. **Heating indicator**: Shows progress bar when current temp < target - 5°C
3. **During steaming**: Calls `startSteamHeating()` for any setting changes
4. **After steaming**: If `keepSteamHeaterOn=false`, calls `turnOffSteamHeater()`
5. **Back button**: Turns off heater if `keepSteamHeaterOn=false`

### Comparison with de1app
- de1app sends `TargetSteamTemp=0` when `steam_disabled=1` or `steam_temperature < 135°C`
- We send 0°C when `steamDisabled=true` or `keepSteamHeaterOn=false` (in Idle)
- Both approaches explicitly turn off the heater rather than relying on machine timeout

## WiFi Scale Discovery (Half Decent Scale)

The HDS can be reached over WiFi as well as BLE. Discovery is DNS-SD, not a
fixed hostname — the scale's mDNS name is user-settable, so a renamed scale is
invisible to a name lookup.

### What the firmware advertises

openscale **v3.0.9+** publishes (`src/wifi_setup.cpp`, `setupMdns()`):

```
_decentscale._tcp   port 80
TXT: fw=<version>  model=hds  name=<mdns_name>  proto=ws  path=/snapshot
instance name: "Half Decent Scale"  or  "Half Decent Scale (<name>)"
```

Older firmware advertises **no service at all**, only `<name>.local`. That is
why the A-record fallback still runs on every scan rather than only when the
browse comes back empty: one LAN can hold both.

### Things the wire does that the firmware source does not suggest

All four were verified against live scales and each has caused a wrong
assumption at least once:

- **A browse lists instances that never resolve.** Stale registrations survive
  in resolver caches when a device reboots or is renamed without sending a
  goodbye — four instances were seen for two live scales. **A PTR hit is not a
  device.** Only show an instance once its SRV *and* address resolve.
- **`fw` is not a bare version.** The value is `FW: 3.1.12` — prefix plus a
  literal space. Strip it; keep anything unparseable verbatim.
- **`name` can be absent.** Firmware 3.1.12 publishes no `name` key despite
  `setupMdns()` appearing to always set one. Every TXT key is optional.
- **DNS-SD suffixes colliding instance names.** Two unrenamed scales appear as
  `Half Decent Scale` and `Half Decent Scale-2`. Neither label identifies a
  physical scale, so such rows must also show their address. Note the
  *hostname* is never suffixed — `hds-2.local` is not generated by anything.

`model=hds` is unauthenticated. It decides what to *show*; it never substitutes
for the `ws://<host><path>` HDS-frame validation before connecting or
persisting.

### Two backends, one interface

`MdnsResolver::browseService()` has two implementations because neither covers
everything:

| Platform | Backend | Why |
|---|---|---|
| iOS, macOS | `DNSServiceBrowse` (Bonjour) | a raw multicast socket on iOS needs `com.apple.developer.networking.multicast`, granted only by application to Apple |
| Android, Windows, Linux | vendored mjansson/mdns | `QHostInfo` cannot enumerate services at all |

macOS compiles **both** and picks at runtime
(`MdnsResolver::setBrowseBackend()`, exposed via the `devices_wifi_browse` MCP
tool), so the mjansson path — which ships to Android and desktop but is
developed on a Mac — can be compared against Bonjour on the same LAN.

**The service type must be listed in `NSBonjourServices` in BOTH
`ios/Info.plist` and `macos/Info.plist`.** macOS builds from the latter;
omitting it makes `DNSServiceBrowse` fail with `kDNSServiceErr_NoAuth`
(-65555), which is indistinguishable from a denied Local Network permission and
will send you to System Settings to fix something that is not broken.

Design rationale and the full verification record live in
`openspec/changes/browse-wifi-scales-dns-sd/design.md`.
