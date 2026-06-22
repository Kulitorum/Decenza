## Why

The Clock widget shipped in #1375 surfaces as **"Clock"** in the layout editor's widget picker, but everywhere else (the placed-chip label, the center-zone caption) it reads **"Time"** — an inconsistency the user noticed. It also renders in a single fixed color (theme text color, i.e. white on the default theme), so it can't be made to stand out or to colour-match the other readouts on the home screen.

## What Changes

- Rename the Clock widget's **editor picker label** from "Clock" to "Time" in both the native QML editor and the web (ShotServer) layout editor, so the picker matches the chip and caption labels that already say "Time".
- Add a **per-instance color option** to **all six readout widgets** — Clock, Temperature, Steam Temp, Water Level, Machine Status, and Scale Weight. The choices are **Default** plus **White**, **Green**, **Red**, **Blue**, **Orange**, mapped to the existing semantic chart colors the rest of the page uses (text/pressure/temperature/flow/warning theme colors) so widgets match the surrounding UI and respect custom themes.
- **"Default" preserves each widget's current color**, including the *dynamic* colors of Machine Status (per machine phase) and Scale Weight (tap/ratio/flow-scale state). A named color is a **full static override** that replaces all coloring, including those state signals — an explicit, opt-in choice.
- The color choice is exposed in the per-instance options editors (alongside the existing text/icon display mode, and Scale Weight's data mode) and applies to both the value text and the optional icon, in every zone, in both the native and web editors.
- The shared mapping lives in one place — a new `WidgetColor` QML singleton and a reusable `WidgetColorPicker` component — so all six widgets and both popups stay in sync.
- Persist the color per widget instance in the layout JSON; unset instances behave as **Default** (today's behaviour) — no migration needed.

## Capabilities

### New Capabilities
- `layout-clock-widget`: The Clock (Time) layout widget's editor picker label reads "Time" (internal `type` id unchanged).
- `layout-readout-widget-colors`: A per-instance color override for the readout layout widgets (clock, temperature, steam temp, water level, machine status, scale weight), with a "Default" choice that preserves each widget's natural (possibly dynamic) color, editable in both the native and web editors.

### Modified Capabilities
<!-- None: the readout widgets' per-instance display config lives in layout-widget-instance-config, but that spec does not enumerate the clock and the color override is an additive new concern, so it is captured as a new capability rather than a delta. -->

## Impact

- **QML (widgets)**: `qml/components/layout/items/{ClockItem,TemperatureItem,SteamTemperatureItem,WaterLevelItem,MachineStatusItem,ScaleWeightItem}.qml` — read `color` and resolve via the shared singleton.
- **QML (shared)**: new `qml/components/layout/WidgetColor.qml` (singleton: palette + resolve/swatch) and `qml/components/layout/WidgetColorPicker.qml` (reusable "Color" section).
- **QML (editors)**: `DisplayModeEditorPopup.qml` and `ScaleWeightEditorPopup.qml` gain the color picker; `SettingsLayoutTab.qml` passes `color` through and routes the clock back to the shared popup (the one-off `ClockEditorPopup.qml` is removed); `LayoutEditorZone.qml` picker label.
- **C++**: `src/network/shotserver_layout.cpp` (web picker label + generalized inline color selector across readout chips + persistence).
- **Build**: `CMakeLists.txt` — register the two new QML files and mark `WidgetColor` a singleton.
- **Persistence**: new optional `color` key on a readout layout item in the layout JSON. Backward compatible — absence means Default.
- **i18n**: new translation keys for the color labels (`Default`/`White`/`Green`/`Red`/`Blue`/`Orange`) and the editor "Color" section; reuse the existing "Time" key for the picker label.
- No BLE, DB, profile, or settings-domain changes.
