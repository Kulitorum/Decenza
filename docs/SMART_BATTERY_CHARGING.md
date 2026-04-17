# Smart Battery Charging

How the Decenza tablet controls the DE1's USB port to maintain the tablet battery in a healthy range.

## Overview

The DE1 exposes its USB charging port via a memory-mapped register (MMR 0x803854 / `setUsbChargerOn`). The tablet reads its own battery level (Android JNI / platform API) and toggles the DE1's charger on or off to keep the battery inside a target window. This extends tablet battery lifespan by avoiding continuous 100% state-of-charge.

```
┌─────────────┐                        ┌─────────────┐
│   Tablet    │                        │     DE1     │
│             │                        │             │
│ Read battery├───── BLE write ───────►│ MMR 0x803854│
│ (platform)  │  setUsbChargerOn 0/1   │ USB Control │
│             │                        │             │
│ target range│◄──── USB Power ────────┤ USB Port    │
└─────────────┘     (on or off)        └─────────────┘
```

## Charging modes

Set via `Settings.batteryChargingMode`. UI lives in `qml/pages/settings/SettingsMachineTab.qml`; the live status widget is `qml/components/layout/items/BatteryLevelItem.qml`.

| Mode | Value | Behavior |
|------|:-----:|----------|
| Off | 0 | Charger always ON (no smart control) |
| On | 1 | Maintain target window (default ~55–65%) |
| Night | 2 | Higher target when active; relax lower bound when machine is asleep |

## Hysteresis (Mode "On")

```
Battery level:  0%────55%────65%────100%
                      │      │
                      ▼      ▼
                 Turn ON    Turn OFF
```

- **Discharging** (charger OFF): wait until battery drops to the low threshold, then turn ON.
- **Charging** (charger ON): wait until battery reaches the high threshold, then turn OFF.
- State tracked via an `m_discharging` flag in `BatteryManager` to prevent rapid on/off cycling.

## The DE1's 10-minute safety timeout

The DE1 firmware automatically re-enables the USB charger 10 minutes after it was last turned off. This is a safety net — if the tablet app crashes, the tablet won't die. To hold the charger OFF we must re-send the "off" command periodically.

`BatteryManager` runs a 60 s timer that re-asserts the desired state every tick. The `force` parameter on `DE1Device::setUsbChargerOn()` / `writeMMR()` bypasses the per-register dedup (see `src/ble/de1device.cpp:1075-1083`) so these refresh writes actually hit the wire even when the value is unchanged. The dedup comment explicitly calls out this case.

## Code flow

```
Timer (60 s) → BatteryManager::checkBattery()
                    │
                    ▼
            readPlatformBatteryPercent()        [Android JNI / platform]
                    │
                    ▼
            applySmartCharging()                [mode + hysteresis logic]
                    │
                    ▼
            DE1Device::setUsbChargerOn(on, force=true)
                    │
                    ▼
            DE1Device::writeMMR(0x803854, value, "setUsbChargerOn")
                    │
                    ▼
            transport queue → BLE write to A006 (WriteToMMR)
```

## BLE command format

Write to characteristic `0000A006-0000-1000-8000-00805F9B34FB`:

```
Byte 0:      0x04              Length (4 bytes)
Bytes 1-3:   0x80 0x38 0x54    Address (big-endian) — 0x803854
Bytes 4-7:   0x01/0x00 + 0s    Value (little-endian, 1=ON, 0=OFF)
Bytes 8-19:  0x00              Padding
```

Equivalent de1app (Tcl) reference:

```tcl
mmr_write "set_usb_charger_on" "803854" "04" [zero_pad [int_to_hex $usbon] 2]
```

## Key files

| File | Purpose |
|------|---------|
| `src/core/batterymanager.h` / `.cpp` | Smart charging logic, 60 s timer, platform battery-level reader |
| `src/ble/de1device.cpp` | `setUsbChargerOn()`, `writeMMR()` with `force` flag |
| `src/ble/protocol/de1characteristics.h` | MMR address constants, BLE UUIDs |
| `qml/pages/settings/SettingsMachineTab.qml` | Mode selector UI |
| `qml/components/layout/items/BatteryLevelItem.qml` | Live battery status layout widget |

## Debug logging

Typical log lines during normal operation:

```
BatteryManager: checkBattery - battery: 60% mode: 1 discharging: false connected: true
BatteryManager: Sending charger command: OFF
[MMR] write: 0x803854 = 0 [setUsbChargerOn]
```

When the port isn't delivering power but the charger is nominally ON, you'll see the warning that was prominent in recent logs:

```
BatteryManager: charger ON but port not delivering power (battery= 60 %, cycle 1 of 5). Retrying.
```

This indicates the BLE command succeeded but the physical USB port isn't supplying current — typically a hardware/cable issue, not a BLE one.

## Testing notes

This feature only works when the tablet is powered by the DE1's USB port. When connected to a PC for development, the PC supplies power and the DE1's charger control has no effect on battery level.

To test properly:

1. Disconnect tablet from PC.
2. Connect tablet only to the DE1's USB port.
3. Set smart charging to "On" in Settings → Machine.
4. Wait for battery to reach the upper threshold — charger should turn OFF (log shows `setUsbChargerOn 0`).
5. Wait for battery to drop to the lower threshold — charger should turn ON.

## Reference: de1app implementation

- `de1plus/utils.tcl` — `check_battery_charger` proc.
- `de1plus/de1_comms.tcl` — `set_usb_charger_on` proc.
- 60 s cadence via `schedule_minute_task`.
