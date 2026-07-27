## BLE Protocol Notes

- DE1 Service: `0000A000-...`
- Command queue prevents BLE overflow (50ms between writes)
- Shot samples at ~5Hz during extraction
- Profile upload: header (5 bytes) + frames (8 bytes each) + extension frames (8 bytes, frame number + 32 for limiters) + tail frame (8 bytes)
- USB charger control: MMR address `0x803854` (1=on, 0=off)
- DE1 has 10-minute timeout that auto-enables charger; must resend command every 60s

### BLE Write Retry & Timeout (like de1app)

BLE writes can fail or hang. The implementation includes retry logic similar to de1app:

**Mechanism:**
- Each write starts a 5-second timeout timer
- On error or timeout: retry up to 3 times with 100ms delay
- After max retries: log failure and move to next command
- Queue is cleared when any flowing operation starts (espresso, steam, hot water, flush)

**Error Logging (captured in shot debug log):**
```
Write timeout, retrying 1/3 (uuid=0000a00f)
Write FAILED after 3 retries (uuid=0000a00f, 8 bytes)
```

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
- de1app uses soft 1-second fallback timer (just retries queue)
- de1app has `vital` flag for commands that must retry
- Our implementation: hard 5-second timeout, all commands can retry up to 3 times

### Connection-Failure Handling

Qt's `QLowEnergyController::disconnected()` signal only fires on a Connected→Disconnected transition — it is **not** emitted when a connection attempt fails part-way (Connecting→Unconnected without ever reaching Connected). To avoid leaving `DE1Device::m_connecting` stuck at `true` forever after a failed retry (which would block all subsequent reconnect attempts and the `de1Discovered` auto-connect path), `BleTransport::setupController()` watches the controller's `stateChanged` signal and synthesizes a `disconnected()` signal when the state reaches `UnconnectedState` without a preceding native `disconnected()`. A flag (`m_disconnectedEmittedForAttempt`) prevents double-emission and is reset to `false` at every point where a fresh BLE-level `QLowEnergyController::connectToDevice()` is about to fire: the outer `BleTransport::connectToDevice()`, the internal service-discovery retry timer, and (defensively) at the tail of `BleTransport::disconnect()`.

Symptom if this is broken: DE1 reboot drops BLE, app attempts one reconnect, the attempt fails, then app stays silent forever until restarted. The Scan Devices button also appears dead because the `de1Discovered` handler's `!isConnecting()` guard never clears.

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
