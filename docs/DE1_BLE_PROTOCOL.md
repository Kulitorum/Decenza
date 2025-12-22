# DE1 BLE Protocol Specification

This document describes the Bluetooth Low Energy protocol used to communicate with the Decent Espresso DE1 machine. This is reverse-engineered from the Tcl codebase and intended as a reference for alternative client implementations.

## Overview

The DE1 machine exposes a BLE GATT service. The tablet app:
1. Connects and subscribes to notifications
2. Uploads espresso profiles (frames) and settings
3. Sends state commands to trigger operations
4. Receives real-time shot data during extraction
5. Implements stop-at-weight using a connected scale

## BLE Service and Characteristics

### Primary Service
`0000A000-0000-1000-8000-00805F9B34FB`

### Characteristics

| Name | UUID Suffix | Access | Description |
|------|-------------|--------|-------------|
| Version | `A001` | Read | Firmware and BLE API version |
| RequestedState | `A002` | Write | Command machine state changes |
| ReadFromMMR | `A005` | Read/Notify | Read memory-mapped registers |
| WriteToMMR | `A006` | Write | Write memory-mapped registers |
| FWMapRequest | `A009` | Write/Notify | Firmware update commands |
| Temperatures | `A00A` | Read/Notify | Temperature readings |
| ShotSettings | `A00B` | Read/Write | Steam, hot water, flush settings |
| ShotSample | `A00D` | Notify | Real-time shot data (~5Hz) |
| StateInfo | `A00E` | Read/Notify | Machine state notifications |
| HeaderWrite | `A00F` | Write | Upload profile header |
| FrameWrite | `A010` | Write | Upload profile frames |
| WaterLevels | `A011` | Read/Notify | Water tank level |
| Calibration | `A012` | Read/Write | Calibration data |

Full UUID format: `0000XXXX-0000-1000-8000-00805F9B34FB` where XXXX is the suffix.

## Machine States

Write to `RequestedState` (A002) to change machine state:

| State | Value | Description |
|-------|-------|-------------|
| Sleep | 0x00 | Low power mode |
| GoingToSleep | 0x01 | Transitioning to sleep |
| Idle | 0x02 | Ready, not heating for operation |
| Busy | 0x03 | Busy with internal operation |
| Espresso | 0x04 | Pulling espresso shot |
| Steam | 0x05 | Steaming milk |
| HotWater | 0x06 | Dispensing hot water |
| ShortCal | 0x07 | Short calibration |
| SelfTest | 0x08 | Self test |
| LongCal | 0x09 | Long calibration |
| Descale | 0x0A | Descaling cycle |
| FatalError | 0x0B | Fatal error state |
| Init | 0x0C | Initializing |
| NoRequest | 0x0D | No state requested |
| SkipToNext | 0x0E | Skip to next profile frame |
| HotWaterRinse | 0x0F | Flush/rinse |
| SteamRinse | 0x10 | Steam wand rinse |
| Refill | 0x11 | Refill water tank |
| Clean | 0x12 | Cleaning cycle |
| InBootLoader | 0x13 | Firmware update mode |
| AirPurge | 0x14 | Air purge (travel mode) |
| SchedIdle | 0x15 | Scheduled wake to idle |

## Substates

Received in `StateInfo` notifications:

| Value | Name | Description |
|-------|------|-------------|
| 0 | ready | Ready |
| 1 | heating | Heating water tank |
| 2 | final heating | Heating group head |
| 3 | stabilising | Stabilizing temperature |
| 4 | preinfusion | Preinfusion phase |
| 5 | pouring | Main extraction/pour |
| 6 | ending | Ending, flush valve active |
| 7 | steaming | Steam flowing |
| 17 | refill | Water refill needed |
| 200+ | Error_* | Various error states |

## Number Formats

The protocol uses fixed-point numbers to fit values in fewer bytes:

### U8P4 (8-bit, 4 fractional bits)
- Range: 0 to 15.9375
- Encode: `round(value * 16)`
- Decode: `byte / 16.0`
- Used for: pressure (bar), flow (mL/s)

### U8P1 (8-bit, 1 fractional bit)
- Range: 0 to 127.5
- Encode: `round(value * 2)`
- Decode: `byte / 2.0`
- Used for: temperature (°C)

### U16P8 (16-bit, 8 fractional bits)
- Range: 0 to 255.996
- Encode: `round(value * 256)`
- Decode: `value / 256.0`
- Used for: precise temperature

### F8_1_7 (custom float for duration)
- If value < 12.8: encode as `round(value * 10)` (0.1s precision)
- If value >= 12.8: encode as `round(value) | 0x80` (1s precision, set high bit)
- Decode: if high bit clear, `byte / 10.0`; else `byte & 0x7F`

### U10P0 (10-bit integer with flag)
- Encode: `round(value) | 1024`
- Decode: `value & 1023`
- Used for: volume limits

## Espresso Profiles

Profiles define how espresso extraction progresses. Only espresso uses the frame system; steam/water use simple settings.

### Profile Header (5 bytes)

Write to `HeaderWrite` (A00F):

```
Byte 0: HeaderV           - Always 1
Byte 1: NumberOfFrames    - Total frames (1-10)
Byte 2: NumberOfPreinfuseFrames - Frames before pour timing starts
Byte 3: MinimumPressure   - U8P4, minimum allowed pressure in flow mode
Byte 4: MaximumFlow       - U8P4, maximum allowed flow in pressure mode
```

### Profile Frame (8 bytes)

Write to `FrameWrite` (A010), one per frame:

```
Byte 0: FrameToWrite      - Frame index (0-9), or 32+ for extension frames
Byte 1: Flag              - Bitfield (see below)
Byte 2: SetVal            - U8P4, target pressure or flow
Byte 3: Temp              - U8P1, target temperature
Byte 4: FrameLen          - F8_1_7, frame duration in seconds
Byte 5: TriggerVal        - U8P4, exit condition threshold
Bytes 6-7: MaxVol         - U10P0, max volume before exit (0 = no limit)
```

### Frame Flags (bitfield)

| Bit | Mask | Name | Meaning when set |
|-----|------|------|------------------|
| 0 | 0x01 | CtrlF | Flow control mode (else pressure) |
| 1 | 0x02 | DoCompare | Enable exit condition |
| 2 | 0x04 | DC_GT | Exit if > threshold (else <) |
| 3 | 0x08 | DC_CompF | Compare flow (else pressure) |
| 4 | 0x10 | TMixTemp | Target mix temp (else basket temp) |
| 5 | 0x20 | Interpolate | Ramp smoothly (else instant jump) |
| 6 | 0x40 | IgnoreLimit | Ignore min pressure/max flow limits |

### Extension Frames

For additional limits (max flow during pressure mode, etc.), send extension frames with `FrameToWrite = original_frame_index + 32`:

```
Byte 0: FrameToWrite      - Original index + 32
Byte 1: MaxFlowOrPressure - U8P4, limit value
Byte 2: MaxFoPRange       - U8P4, acceptable range
Bytes 3-7: Padding        - Zero
```

### Tail Frame

After all frames, send a tail frame:

```
Byte 0: FrameToWrite      - NumberOfFrames (next index)
Bytes 1-2: MaxTotalVolume - U10P0, max total shot volume
Bytes 3-7: Padding        - Zero
```

### Example: 3-Frame Profile

```
Header: 01 03 01 00 60
  HeaderV=1, 3 frames, 1 preinfuse frame, MinPressure=0, MaxFlow=6.0

Frame 0 (Preinfusion): 00 2E 40 BA 64 28 00 00
  Flag=0x2E (DoCompare|DC_GT|DC_CompF|Interpolate)
  SetVal=4.0 bar, Temp=93°C, FrameLen=10s
  TriggerVal=2.5 mL/s (exit if flow exceeds), MaxVol=0

Frame 1 (Ramp): 01 20 90 B8 32 00 00 00
  Flag=0x20 (Interpolate)
  SetVal=9.0 bar, Temp=92°C, FrameLen=5s
  No exit condition

Frame 2 (Pour): 02 00 90 B8 C8 00 00 00
  Flag=0x00 (pressure mode, no exit, instant)
  SetVal=9.0 bar, Temp=92°C, FrameLen=20s

Tail: 03 00 00 00 00 00 00 00
```

## Steam/Hot Water Settings

Write to `ShotSettings` (A00B), 9 bytes:

```
Byte 0: SteamSettings     - Flags (reserved)
Byte 1: TargetSteamTemp   - U8P0, steam temperature °C
Byte 2: TargetSteamLength - U8P0, max steam time seconds
Byte 3: TargetHotWaterTemp - U8P0, hot water temperature °C
Byte 4: TargetHotWaterVol - U8P0, hot water volume mL
Byte 5: TargetHotWaterLength - U8P0, max hot water time seconds
Byte 6: TargetEspressoVol - U8P0, typical espresso volume (for display)
Bytes 7-8: TargetGroupTemp - U16P8, group head temperature °C
```

## Real-Time Shot Data (ShotSample)

Subscribe to notifications on `ShotSample` (A00D). Received at ~5Hz during operations:

The data contains:
- Current pressure
- Current flow rate
- Mix temperature
- Head temperature
- Current profile frame number
- Cumulative volume dispensed
- Shot timer value

(Exact byte layout requires further reverse engineering of `update_de1_shotvalue` in the codebase)

## Memory-Mapped Registers (MMR)

For advanced configuration, use `ReadFromMMR` (A005) and `WriteToMMR` (A006).

### MMR Read Request (20 bytes)
```
Byte 0: Len              - Data length in words-1 (0=4 bytes, 1=8 bytes)
Bytes 1-3: Address       - 24-bit address (big endian)
Bytes 4-19: Data         - Unused for read requests
```

### MMR Write Request (20 bytes)
```
Byte 0: Len              - Data length (typically 0x04)
Bytes 1-3: Address       - 24-bit address (big endian)
Bytes 4-19: Data         - Data to write (little endian integers)
```

### Common MMR Addresses

| Address | Name | Description |
|---------|------|-------------|
| 0x800008 | CPUBoardModel | CPU board model × 1000 |
| 0x80000C | MachineModel | 1=DE1, 2=DE1Plus, 3=DE1Pro, 4=DE1XL, 5=DE1Cafe |
| 0x800010 | FirmwareVersion | Build number (starts at 1000) |
| 0x803808 | FanThreshold | Fan activation temperature |
| 0x80380C | TankTempThreshold | Tank temperature setting |
| 0x803810 | Phase1FlowRate | Heating phase 1 flow |
| 0x803814 | Phase2FlowRate | Heating phase 2 flow |
| 0x803818 | HotWaterIdleTemp | Hot water idle temperature |
| 0x80381C | GHCInfo | Group Head Controller: 0x1=present, 0x2=active |
| 0x803820 | GHCMode | GHC operating mode |
| 0x803828 | SteamFlow | Steam flow rate setting |
| 0x80382C | SteamHighflowStart | Seconds before high steam flow |
| 0x803830 | SerialNumber | Machine serial number |
| 0x803834 | HeaterVoltage | Heater voltage setting |
| 0x803838 | EspressoWarmupTimeout | Warmup timeout seconds |
| 0x803840 | FlushFlowRate | Flush flow rate × 10 |
| 0x803848 | FlushTimeout | Flush timeout × 10 |
| 0x80384C | HotWaterFlowRate | Hot water flow rate × 10 |
| 0x80385C | RefillKitPresent | 0=no, 1=yes, 2=auto-detect |

## Scale Integration

Scales are separate BLE devices. Each brand has its own service/characteristic UUIDs:

### Decent Scale
- Service: `0000FFF0-0000-1000-8000-00805F9B34FB`
- Read: `0000FFF4-...`
- Write: `000036F5-...`

### Acaia Pyxis
- Service: `49535343-FE7D-4AE5-8FA9-9FAFD205E455`
- Status: `49535343-1E4D-4BD9-BA61-23C647249616`
- Command: `49535343-8841-43F4-A8D4-ECBE34729BB3`

### Felicita
- Service: `0000FFE0-0000-1000-8000-00805F9B34FB`
- Characteristic: `0000FFE1-...`

(See `machine.tcl` for all scale UUIDs)

## Connection Sequence

1. Scan for device advertising the DE1 service UUID
2. Connect and discover services
3. Subscribe to notifications: StateInfo, ShotSample, WaterLevels, ReadFromMMR
4. Read Version characteristic
5. Read MMR values: GHC info, firmware version, serial number
6. Send Idle state command to wake machine
7. Upload current profile and settings
8. Machine is ready for operation

## Typical Operation Flow

### Pulling an Espresso Shot

1. Upload profile via HeaderWrite + FrameWrite
2. Upload settings via ShotSettings
3. Write `0x04` to RequestedState (Espresso)
4. Machine heats, then begins extraction
5. Receive ShotSample notifications with real-time data
6. Receive StateInfo notifications for substate changes
7. (Optional) Write `0x02` (Idle) to stop early for stop-at-weight
8. Machine completes and returns to Idle

### Steaming

1. Upload settings via ShotSettings (steam temp, duration)
2. Write `0x05` to RequestedState (Steam)
3. Machine heats steam boiler
4. Steam flows until timeout or Idle command
5. Machine returns to Idle

## Source Code Reference

| File | Contents |
|------|----------|
| `binary.tcl` | Binary protocol encoding/decoding, frame structures |
| `machine.tcl` | UUIDs, state definitions, machine control functions |
| `de1_comms.tcl` | BLE command queue, MMR read/write, event handling |
| `bluetooth.tcl` | BLE connection management, scale protocols |
| `profile.tcl` | Profile structure, conversion to frames |
