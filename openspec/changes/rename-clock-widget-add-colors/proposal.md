## Why

The Clock widget shipped in #1375 surfaces as **"Clock"** in the layout editor's widget picker, but everywhere else (the placed-chip label, the center-zone caption) it reads **"Time"** — an inconsistency the user noticed. It also renders in a single fixed color (theme text color, i.e. white on the default theme), so it can't be made to stand out or to colour-match the other readouts on the home screen.

## What Changes

- Rename the Clock widget's **editor picker label** from "Clock" to "Time" in both the native QML editor and the web (ShotServer) layout editor, so the picker matches the chip and caption labels that already say "Time".
- Add a **per-instance color option** to the Clock widget. In addition to the current default (**White** / theme text color), the user can choose **Green**, **Red**, **Blue**, or **Orange** — mapped to the existing semantic chart colors the rest of the page uses (pressure/flow/temperature/warning theme colors) so the clock matches the surrounding UI and respects custom themes.
- The color choice is exposed in the per-instance options editor (alongside the existing text/icon display mode) and applies to both the time text and the optional clock icon, in every zone, in both the native and web editors.
- Persist the color per widget instance in the layout JSON; unset instances default to White (today's behaviour) — no migration needed.

## Capabilities

### New Capabilities
- `layout-clock-widget`: The Clock (Time) layout widget — its editor picker label and its per-instance configuration (display mode plus the new color choice), covering both the native QML editor and the web layout editor.

### Modified Capabilities
<!-- None: the Clock widget shipped without a dedicated spec; its per-instance config is captured as a new capability rather than amending layout-widget-instance-config (which does not currently enumerate the clock). -->

## Impact

- **QML**: `qml/components/layout/items/ClockItem.qml` (read + apply color), a per-instance editor popup for the clock (new dedicated popup, or extend the routing in `qml/pages/settings/SettingsLayoutTab.qml`), `qml/pages/settings/LayoutEditorZone.qml` (picker label).
- **C++**: `src/network/shotserver_layout.cpp` (web picker label + inline color selector + persistence), `src/core/settings_network.cpp` (clock already in `kConfigurable`; no change expected).
- **Persistence**: new optional `color` key on the clock layout item in the layout JSON (read by both editors and the runtime widget). Backward compatible — absence means White.
- **i18n**: new translation keys for the color labels and the editor section; reuse the existing "Time" key for the picker label.
- No BLE, DB, profile, or settings-domain changes.
