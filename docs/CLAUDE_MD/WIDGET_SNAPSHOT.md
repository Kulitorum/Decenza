# Machine-status widget snapshot

The Qt app publishes a compact JSON snapshot to platform-shared storage on
meaningful state changes. The iOS WidgetKit extension and the Android
`AppWidgetProvider` read it. It is disposable cache — never user data.

This is the stable home for the schema (the spec originated in the
`add-machine-status-widget` OpenSpec change; it lives here so code comments
don't rot when that change is archived).

## Code map

| Concern | File |
|---------|------|
| Writer + value type + single serializer | `src/widget/machinestatussnapshot.{h,cpp}` (`WidgetSnapshot::toJson`) |
| Shared transport keys (source of truth) | `src/widget/widgetsharedkeys.h` |
| iOS write bridge | `ios/MachineStatusWidgetBridge.mm` (includes `widgetsharedkeys.h`) |
| iOS reader / widget | `ios/widget/MachineStatusSnapshot.swift`, `DecenzaWidget.swift` |
| Android write bridge | `MachineStatusWidget.java` |
| Android reader / widget | `MachineStatusWidgetProvider.java` |

`widgetsharedkeys.h` is the single source of truth for the **transport keys**.
The Android Java helper and the iOS Swift widget mirror those strings by hand;
the iOS ObjC++ bridge `#include`s the header directly (not a mirror).

## Transport

| Platform | Location |
|----------|----------|
| iOS | App Group `UserDefaults` (`group.io.github.kulitorum.decenza`), key `machineStatusSnapshot` |
| Android | `SharedPreferences` file `decenza_widget`, key `machine_status_snapshot` |
| Desktop | `<AppDataLocation>/widget/machine_status.json` (no widget; parity/testing) |

## Fields

| Field | Type | Units / format | Notes |
|-------|------|----------------|-------|
| `schemaVersion` | int | — | Reserved for a future *incompatible* change. Consumers do **not** branch on it today; forward/backward safety comes from fail-closed parsing, not version negotiation. |
| `phase` | string | enum | `MachineState::phaseString()` (raw `Phase` enum key) |
| `connected` | bool | — | `DE1Device::isConnected()` at capture |
| `temperatureC` | number? | °C | group head; omitted if no device |
| `targetTemperatureC` | number? | °C | `goalTemperature()` |
| `steamTemperatureC` | number? | °C | `steamTemperature()` |
| `lastShot` | object \| absent | — | absent until a shot is finalized (espresso only) |
| `lastShot.yieldG` | number | grams | finite, ≥ 0 (validated writer-side) |
| `lastShot.durationSec` | number | seconds | finite, ≥ 0 |
| `lastShot.qualityBadge` | string \| absent | — | present only when non-empty |
| `capturedAt` | string | ISO-8601 with UTC offset (or `Z`) | `dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)` |

## Phase labels

The canonical source of `phase` values is the C++ `MachineState::Phase`
enum. Consumers map each raw string to a short widget label so it fits the
smallest widget size without truncation. This table, the iOS
`WidgetPhase.labels`, and the Android `MachineStatusWidgetProvider.PHASE_LABELS`
are all hand-maintained mirrors of that enum — when a `Phase` enumerator is
added/renamed, update all three (and this table). Unknown values fall back to
the raw string by design, so a missed entry degrades to a longer label, not a
crash.

| Raw `phase` | Widget label |
|-------------|--------------|
| Disconnected | Disconnected |
| Sleep | Sleep |
| Idle | Idle |
| Heating | Heating |
| Ready | Ready |
| EspressoPreheating | Preheating |
| Preinfusion | Preinfusion |
| Pouring | Pouring |
| Ending | Finishing |
| Steaming | Steaming |
| HotWater | Hot Water |
| Flushing | Flushing |
| Refill | Refill |
| Descaling | Descaling |
| Cleaning | Cleaning |

## Display rules (consumers)

1. Snapshot missing/unparsable, or `connected == false` → **Disconnected — Tap to open**; never render a temperature as current.
2. `connected` and `now − capturedAt` > freshness window (3 min) → render with an **`updated N min ago`** annotation.
3. Fresh + connected → phase + temperature-vs-target + last-shot, no annotation.

Temperature line: `Heating` → `Heating <cur> → <target> °C`; `Ready` → `Ready · <cur> °C`; `Steaming` → `<steam> °C steam`; else `<cur> °C`.

Size-adaptive (not user-configurable by design): the smallest size
(iOS `systemSmall`, Android < 120 dp tall) shows only phase + temperature;
medium and larger add the last-shot and staleness lines.
