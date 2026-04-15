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

### Shot Debug Logging

`ShotDebugLogger` captures all `qDebug()`/`qWarning()` messages during shots:
- Installs Qt message handler when shot starts
- Stores captured log in `debug_log` column of shot history
- Users can view/export via shot history web interface
- BLE errors are automatically captured (use `qWarning()` for errors)

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
