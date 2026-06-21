## Why

PR #1362 ships an "optional compact status bar" as a hardcoded ~166-line bar in `StatusBar.qml` behind a new `compactStatusBar` theme flag — the same hardcode-behind-a-flag shape we replaced for the home screen in #1368. Every piece of that bar already exists as a layout widget (machine status, group/steam temp, scale weight, battery, sleep), and the top bar's height doesn't change (the "compact" look is a content *style*, not a smaller zone). So we can deliver the icon-led bar entirely through the existing layout editor by giving those existing widgets **per-instance display options** — no new widgets, no flag, and the options are reusable in any zone, not just the status bar.

## What Changes

- **Add a per-instance `displayMode` to the readout widgets** `machineStatus`, `temperature`, `steamTemperature`, and `scaleWeight`: `text` (default, current look) or `icon` (a tinted icon ahead of the value). Read from the item's stored properties (`modelData`), so two instances can differ and the icon form works in **any** zone — the status bar, a mixed bottom bar, the lower-mid bar, anywhere. Icons already exist (`decent-de1.svg`, `temperature.svg`, `steam.svg`, `scale.svg`).
- **Edit `displayMode` via long-press in the layout editor** (in-app + web), reusing the existing per-instance editor path (`setItemProperty`, like `scaleWeight`'s `dataMode`). `scaleWeight` exposes both its `dataMode` and the new `displayMode`.
- **Per-instance Sleep widget configuration**: long-press the Sleep widget to **remove the quit (long-press-to-quit) option** for that instance (`allowQuit`, default true), so a centred Sleep can be single-tap-only with no hidden exit.
- **Add a "Compact status bar" populate preset** to the zone-options panel (in-app + web): fills the `statusBar` zone with machine status · group temp · steam temp · scale · battery (all `displayMode: icon`) and a spacer-centred Sleep — reproducing #1362's look in one tap. Uses the existing populate mechanism and the existing `StatusBar.qml` renderer (no refactor: the current Repeater already honors spacers and per-instance item properties).
- After this lands, PR #1362's `compactStatusBar` flag, the hardcoded bar, and the `SleepItem` collapse are unnecessary; the compact bar is just a `statusBar` layout — user-composed or one-tap via the preset.

## Capabilities

### New Capabilities
- `layout-machine-status-widget`: Merge the `connectionStatus` widget into `machineStatus` (one widget, alias + migration) — the machine-status display already shows "Disconnected" when offline, subsuming Online/Offline.

### Modified Capabilities
- `layout-widget-instance-config`: add a per-instance `displayMode` (text / icon) for the `machineStatus`, `temperature`, `steamTemperature`, and `scaleWeight` widgets, and a per-instance quit toggle for the `sleep` widget — all via the long-press editor.
- `layout-zone-configuration`: add a "Compact status bar" populate preset that fills a zone (offered for `statusBar`) with the icon-mode readouts + a centred Sleep.

## Impact

- **QML widgets**: `MachineStatusItem.qml`, `TemperatureItem.qml`, `SteamTemperatureItem.qml`, `ScaleWeightItem.qml` (read `modelData.displayMode`; render icon ahead of value when `icon`); `SleepItem.qml` (read `modelData.allowQuit`; gate the quit paths). All keep their current default behaviour.
- **QML editor**: a shared per-instance display-mode editor popup (and `scaleWeight`'s editor also exposes `dataMode`); a Sleep instance editor (quit toggle); `LayoutEditorZone.qml`/`SettingsLayoutTab.qml` route long-press for these widget types; `ZoneOptionsPopup.qml` gains the "Compact status bar" populate action.
- **C++**: `src/core/settings_network.cpp` and `src/network/shotserver_layout.cpp` — "Compact status bar" preset in the populate path; per-instance properties already supported (`setItemProperty`/`getItemProperties`).
- **Web editor**: `src/network/shotserver_layout.cpp` — inline `displayMode` selector on the readout chips, inline quit toggle on the sleep chip, and the Compact-status-bar populate button.
- **No `StatusBar.qml` refactor**: the existing Repeater renders the populated `statusBar` zone and honors spacers + per-instance item properties as-is.
- **PR #1362**: superseded — `compactStatusBar` (`settings_theme.{h,cpp}`, `settingsserializer.cpp`) and the hardcoded bar are dropped; built composably on `main`.
- **No change for existing users**: every new option defaults to current behaviour; the compact look is opt-in.
