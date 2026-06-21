## 1. Per-instance display mode on readout widgets

- [x] 1.1 `MachineStatusItem.qml`: read `modelData.displayMode` (default `text`); in `icon` mode render `decent-de1.svg` (or state icon) ahead of the value, tinted to the surrounding text color
- [x] 1.2 `TemperatureItem.qml`: same, with `temperature.svg`
- [x] 1.3 `SteamTemperatureItem.qml`: same, with `steam.svg`
- [x] 1.4 `ScaleWeightItem.qml`: same, with `scale.svg` (add `modelData`/`displayMode`; keep existing `dataMode`)
- [x] 1.5 Apply the icon in the bar/compact render path (and the full path if the widget renders there); default `text` = unchanged
- [x] 1.6 Preserve each widget's disconnected placeholder ("—"/"--") and accessibility in both modes

## 2. Editors for display mode

- [x] 2.1 Create a shared `DisplayModeEditorPopup.qml` (text/icon toggle) for machineStatus/temperature/steamTemperature; register in `CMakeLists.txt`
- [x] 2.2 Add a `displayMode` row to `ScaleWeightEditorPopup.qml` (alongside `dataMode`)
- [x] 2.3 Register `machineStatus`/`temperature`/`steamTemperature`/`scaleWeight` as configurable so long-press routes to the right editor (`LayoutEditorZone.qml` + `SettingsLayoutTab.qml` `openCustomEditor` routing)
- [x] 2.4 Web editor: inline `displayMode` selector on machineStatus/temperature/steamTemperature/scaleWeight chips, persisting via `/api/layout/item`

## 3. Per-instance Sleep quit toggle

- [x] 3.1 `SleepItem.qml`: read `modelData.allowQuit` (default true); when false, disable `onPressAndHold`/`onAccessibleLongPressed` quit and drop the "long-press to quit" accessibility hint
- [x] 3.2 Create `SleepEditorPopup.qml` (quit-option toggle) writing via `setItemProperty`; register in `CMakeLists.txt` and wire long-press routing for `sleep`
- [x] 3.3 Web editor: inline quit toggle on a selected `sleep` chip, persisting via `/api/layout/item`
- [ ] 3.4 Verify two `sleep` instances can hold different quit settings independently

## 4. Compact status bar populate preset

- [x] 4.1 Add a `compactStatusBar` preset to the C++ populate path (`settings_network` / `shotserver_layout` zone-populate): machineStatus · temperature · steamTemperature · scaleWeight · batteryLevel (each `displayMode: icon`) with spacer · sleep · spacer to centre Sleep
- [x] 4.2 Add the preset to the in-app `ZoneOptionsPopup` populate action (offer for statusBar)
- [x] 4.3 Add the preset to the web editor zone-options populate
- [x] 4.4 Confirm the existing `StatusBar.qml` Repeater renders the populated zone correctly (spacers centre Sleep; per-instance `displayMode` honored) — no renderer change

## 5. Supersede PR #1362

- [x] 5.1 Confirm no `compactStatusBar` flag / hardcoded bar / `SleepItem` collapse is introduced (build composably on `main`)
- [ ] 5.2 (Optional) Ship a "Compact status bar" default `statusBar` config as data, not a code path

## 6. Verification

- [ ] 6.1 Default widgets/layout are byte-for-byte unchanged (all new options default to current behaviour)
- [ ] 6.2 "Compact status bar" populate reproduces the #1362 look (icon-led, centred Sleep) in the in-app editor
- [ ] 6.3 Same in the web editor; confirm in-app/web parity for the preset, display mode, and sleep toggle
- [ ] 6.4 An icon-mode readout works when placed in a different (mixed) zone, e.g. the lower-mid bar
- [ ] 6.5 A Sleep instance with quit removed sleeps on tap and does not quit on long-press; another instance keeps quit
- [ ] 6.6 Two readout instances with different `displayMode` render independently
- [x] 6.7 Compile via Qt Creator; clear QML warnings/TypeErrors for touched files
