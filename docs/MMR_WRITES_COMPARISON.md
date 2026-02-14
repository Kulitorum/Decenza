# MMR Writes: Decenza vs de1app Comparison

Comprehensive comparison of all Memory-Mapped Register (MMR) writes between the original de1app (Tcl/Tk) and Decenza (Qt/C++). MMR writes use BLE characteristic `0000A006` (WRITE_TO_MMR) with a 20-byte format: length (1 byte) + address (3 bytes, big-endian) + value (4 bytes, little-endian) + padding.

## Writes Both Apps Perform

| Address | Name | Decenza Constant | de1app Variable | Default | Triggers |
|---------|------|------------------|-----------------|---------|----------|
| `0x803808` | Fan threshold | `MMR::FAN_THRESHOLD` | `fan_threshold` | 60 | Connection |
| `0x803810` | Phase 1 flow rate (heater warmup) | `MMR::PHASE1_FLOW_RATE` | `phase_1_flow_rate` | 20 | Connection + setting change |
| `0x803814` | Phase 2 flow rate (heater test) | `MMR::PHASE2_FLOW_RATE` | `phase_2_flow_rate` | 40 | Connection + setting change |
| `0x803818` | Hot water idle temp | `MMR::HOT_WATER_IDLE_TEMP` | `hot_water_idle_temp` | 990 (99.0 C) | Connection + setting change |
| `0x803820` | GHC mode | `MMR::GHC_MODE` | `ghc_mode` | 1 | Operation start (espresso/steam/hotwater/flush) |
| `0x803828` | Steam flow rate | `MMR::STEAM_FLOW` | `steam_flow` | varies | Setting change |
| `0x803838` | Espresso warmup timeout | `MMR::ESPRESSO_WARMUP_TIMEOUT` | `espresso_warmup_timeout` | 10 | Connection + setting change |
| `0x80383C` | Flow calibration multiplier | `MMR::FLOW_CALIBRATION` | `calibration_flow_multiplier` | 1000 (1.0x) | Connection + setting change |
| `0x803840` | Flush flow rate | *(literal)* | `flush_flow` | varies | Connection + setting change |
| `0x803848` | Flush timeout | *(literal)* | `flush_seconds` | varies | Connection + setting change |
| `0x80384C` | Hot water flow rate | `MMR::HOT_WATER_FLOW_RATE` | `hotwater_flow` | 10 (1.0 mL/s) | Connection + setting change |
| `0x803850` | Steam two-tap stop | `MMR::STEAM_TWO_TAP_STOP` | `steam_two_tap_stop` | 0 (off) | Connection + setting change |
| `0x803854` | USB charger on/off | `MMR::USB_CHARGER` | n/a | 1 | Periodic (60s) + connection |
| `0x80385C` | Refill kit present | `MMR::REFILL_KIT` | n/a | auto | Connection + setting change |

### Heater Calibration (6 settings in UI)

These are the settings shown in Settings > Options > Heater Calibration popup:

| Setting | Address | Decenza Property | de1app Variable | Default | Unit |
|---------|---------|------------------|-----------------|---------|------|
| Heater idle temperature | `0x803818` | `Settings::heaterIdleTemp` | `hot_water_idle_temp` | 990 | tenths C (displayed as 99.0 C) |
| Heater warmup flow rate | `0x803810` | `Settings::heaterWarmupFlow` | `phase_1_flow_rate` | 20 | tenths mL/s (displayed as 2.0 mL/s) |
| Heater test flow rate | `0x803814` | `Settings::heaterTestFlow` | `phase_2_flow_rate` | 40 | tenths mL/s (displayed as 4.0 mL/s) |
| Heater test timeout | `0x803838` | `Settings::heaterWarmupTimeout` | `espresso_warmup_timeout` | 10 | tenths s (displayed as 1.0 s) |
| Hot water flow rate | `0x80384C` | `Settings::hotWaterFlowRate` | `hotwater_flow` | 10 | tenths mL/s (displayed as 1.0 mL/s) |
| Steam two-tap stop | `0x803850` | `Settings::steamTwoTapStop` | `steam_two_tap_stop` | false (0) | boolean: 0=off, 1=two taps to stop |

**Timing comparison for heater calibration:**
- **de1app:** Sent on connection via `set_heater_tweaks()` (called from `later_new_de1_connection_setup`), and when user clicks "Done" on calibration page.
- **Decenza:** Sent on connection via `applyAllSettings()` (1s after `initialSettingsComplete`), and immediately on any slider/toggle change via signal/slot.
- **Verdict:** Equivalent behavior.

## Writes de1app Does That Decenza Does NOT

### Setting-only (sent only when user explicitly changes a setting)

| Address | Name | de1app Variable | When Sent | Impact |
|---------|------|-----------------|-----------|--------|
| `0x80380C` | Tank temperature threshold | `tank_temperature_threshold` | User changes preheat temp | Controls tank preheat target. de1app uses a special 2-phase write: sends 60 C first, then the real value (if >= 10 C) to force water circulation. Not sent on connection. |
| `0x80382C` | Steam high-flow start | *(param)* | User changes steam settings | Controls when steam transitions to high flow. Only adjustable in some de1app skins. Not commonly changed. |
| `0x803834` | Heater voltage | *(param)* | User sets 120V/230V | Tells machine what mains voltage it's on. Decenza defines `MMR::HEATER_VOLTAGE` but only reads it, never writes. |

### Negligible / model-specific

| Address | Name | de1app Variable | Notes |
|---------|------|-----------------|-------|
| `0x803858` | Feature flags (UserNotPresent) | *(param)* | Commented out in de1app, not in common use |
| `0x803860` | User is present | always `1` | Rarely called in de1app |
| `0x803874` | Cup warmer temperature | `cupwarmer_temp` | BENGLE model only (integrated cup warmer hardware) |

## de1app Function Reference

### `set_heater_tweaks()` (de1_comms.tcl)

Called from `later_new_de1_connection_setup()` and calibration UI. Writes:
- `0x803810` phase_1_flow_rate
- `0x803814` phase_2_flow_rate
- `0x803818` hot_water_idle_temp
- `0x803838` espresso_warmup_timeout
- `0x803850` steam_two_tap_stop
- Then calls: `set_flush_timeout()`, `set_flush_flow_rate()`, `set_hotwater_flow_rate()`

### `save_settings_to_de1()` (de1_comms.tcl)

Called when user saves general settings (profile changes, temperature, etc.). Does **NOT** call `set_heater_tweaks()`. Only calls:
- `de1_send_shot_frames` (profile upload)
- `de1_send_steam_hotwater_settings` (ShotSettings BLE characteristic)

### `later_new_de1_connection_setup()` (bluetooth.tcl)

Full connection sequence:
1. Enable BLE notifications
2. `de1_send_shot_frames` (upload profile)
3. `de1_send_steam_hotwater_settings` (steam/hot water targets)
4. `de1_send_waterlevel_settings`
5. `set_heater_tweaks` (MMR writes: heater cal + flush + hot water flow + steam two-tap)
6. `set_calibration_flow_multiplier` (MMR write)
7. `get_heater_voltage` (MMR read)
8. `set_fan_temperature_threshold` (MMR write)

## Decenza Function Reference

### `applyHeaterTweaks()` (maincontroller.cpp)

Called on connection (via `applyAllSettings`) and on any heater calibration setting change (signal/slot). Writes:
- `0x803810` PHASE1_FLOW_RATE
- `0x803814` PHASE2_FLOW_RATE
- `0x803818` HOT_WATER_IDLE_TEMP
- `0x803838` ESPRESSO_WARMUP_TIMEOUT
- `0x80384C` HOT_WATER_FLOW_RATE
- `0x803850` STEAM_TWO_TAP_STOP

### `applyAllSettings()` (maincontroller.cpp)

Called 1 second after `initialSettingsComplete` signal. Full sequence:
1. Upload current profile (if FrameBased)
2. `applySteamSettings()` (steam temp + steam flow MMR + flush timeout reset)
3. `applyHotWaterSettings()` (hot water temp via ShotSettings + flush timeout reset)
4. `applyFlushSettings()` (flush flow + flush timeout MMRs)
5. `applyWaterRefillLevel()`
6. `applyRefillKitOverride()` (refill kit MMR)
7. `applyFlowCalibration()` (flow calibration MMR)
8. `applyHeaterTweaks()` (6 heater calibration MMRs)

## Key Differences Summary

1. **Tank temperature threshold (`0x80380C`)** — de1app writes when user changes preheat setting (with special 2-phase write for circulation). Decenza never writes this. Only matters if user wants to adjust preheat behavior.

2. **Steam high-flow start (`0x80382C`)** — de1app writes from some skin UIs. Decenza never writes this. Uncommon setting.

3. **Heater voltage (`0x803834`)** — de1app writes when user sets voltage. Decenza reads but never writes. Only matters during initial machine setup.
