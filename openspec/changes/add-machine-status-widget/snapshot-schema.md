# Machine-status snapshot schema

The Qt app publishes this compact JSON object to platform-shared storage on
meaningful state changes. Both the iOS WidgetKit extension and the Android
`AppWidgetProvider` read it. It is disposable cache — never user data.

## Transport

| Platform | Location | Written by |
|----------|----------|------------|
| iOS | App Group shared `UserDefaults` (`group.io.github.kulitorum.decenza`), key `machineStatusSnapshot` | ObjC++ bridge → `[NSUserDefaults setObject:forKey:]` |
| Android | `SharedPreferences` file `decenza_widget`, key `machine_status_snapshot` | Java helper `MachineStatusWidget.writeSnapshot(String)` via QJniObject |
| Desktop | `<AppDataLocation>/widget/machine_status.json` | `QFile` (no widget; kept for parity/manual testing) |

The exact strings live in **one** place per layer:
- C++: `src/widget/widgetsharedkeys.h`
- Android Java: `MachineStatusWidget` constants (mirrors the header)
- iOS Swift: a `WidgetKeys` constant (mirrors the header)

## Fields

| Field | Type | Units / format | Notes |
|-------|------|----------------|-------|
| `schemaVersion` | int | — | Bump on incompatible change; widgets ignore unknown future versions gracefully |
| `phase` | string | enum | `MachineState::phaseString()` value (e.g. `Sleep`, `Idle`, `Heating`, `Ready`, `Pouring`, `Steaming`, `HotWater`, `Flushing`, …) |
| `connected` | bool | — | `DE1Device::isConnected()` at capture time |
| `temperatureC` | number | °C | Group head temperature (`DE1Device::temperature()`) |
| `targetTemperatureC` | number | °C | `DE1Device::goalTemperature()` |
| `steamTemperatureC` | number | °C | `DE1Device::steamTemperature()` |
| `lastShot` | object \| absent | — | Omitted entirely when no shot has completed this session |
| `lastShot.yieldG` | number | grams | Final weight of the most recent shot |
| `lastShot.durationSec` | number | seconds | Extraction duration of the most recent shot |
| `lastShot.qualityBadge` | string \| absent | — | Present only when a badge is available; omitted otherwise |
| `capturedAt` | string | ISO-8601 with UTC offset | `dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)` per project MCP convention |

## Widget display rules (consumers)

1. Snapshot missing or unparsable, or `connected == false` → show **Disconnected — Tap to open**; never render a temperature as current.
2. `connected == true` and `now − capturedAt` > freshness window → render normally with an **`updated N min ago`** annotation.
3. Fresh + connected → render phase, temperature-vs-target, and last-shot summary with no staleness annotation.

Temperature-vs-target formatting (consumer side, from raw fields):
- `phase == Heating` → `Heating <temperatureC> → <targetTemperatureC> °C`
- at-temperature / `Ready` → Ready indicator + `temperatureC`
- `phase == Steaming` → `steamTemperatureC`

## Example

```json
{
  "schemaVersion": 1,
  "phase": "Heating",
  "connected": true,
  "temperatureC": 84.2,
  "targetTemperatureC": 93.0,
  "steamTemperatureC": 21.5,
  "lastShot": { "yieldG": 36.1, "durationSec": 28.4 },
  "capturedAt": "2026-05-18T09:04:31-06:00"
}
```
