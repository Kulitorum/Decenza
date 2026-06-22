## 1. Rename picker label to "Time"

- [x] 1.1 In `qml/pages/settings/LayoutEditorZone.qml`, change the clock catalog entry label from "Clock" to "Time" (reuse the existing `layoutEditor.chipTime` key). Chip label already reads "Time".
- [x] 1.2 In `src/network/shotserver_layout.cpp`, change the clock `WIDGET_TYPES` catalog entry label from "Clock" to "Time" (`DISPLAY_NAMES` already shows `clock:"Time"`).

## 2. Shared color infrastructure

- [x] 2.1 Add `qml/components/layout/WidgetColor.qml` singleton: `choices` palette (default/white/green/red/blue/orange), `resolve(name, fallback)` mapping named→theme color and default/unknown→fallback, and `swatch(name)` for the picker preview.
- [x] 2.2 Add `qml/components/layout/WidgetColorPicker.qml`: reusable "Color" section that renders the palette as swatches and persists via `Settings.network.setItemProperty(itemId, "color", value)`.
- [x] 2.3 Register both QML files in `CMakeLists.txt` and mark `WidgetColor.qml` a singleton with `QT_QML_SINGLETON_TYPE`.

## 3. Apply color to the readout widgets

- [x] 3.1 In each of `ClockItem`, `TemperatureItem`, `SteamTemperatureItem`, `WaterLevelItem`, `MachineStatusItem`, add `colorChoice` (default "default") and resolve the value/icon color via `WidgetColor.resolve(colorChoice, <natural>)`. Leave secondary/caption colors untouched.
- [x] 3.2 In `ScaleWeightItem`, add `colorChoice` and apply the override inside `scaleColor()` so the static override also covers the tap/ratio/flow-scale states; "default" keeps them.

## 4. Native editors expose Color

- [x] 4.1 Add the `WidgetColorPicker` to `DisplayModeEditorPopup.qml` and extend `openForItem(id, mode, color)`. Widen the popup for the 6-swatch row.
- [x] 4.2 Add the `WidgetColorPicker` to `ScaleWeightEditorPopup.qml` and extend `openForItem(id, dataMode, display, color)`.
- [x] 4.3 In `SettingsLayoutTab.qml`, pass `props.color` through both popups and route `clock` back to the shared `DisplayModeEditorPopup`. Remove the one-off `ClockEditorPopup.qml`, its declaration, and its `closeOptionEditors()` entry, and drop it from `CMakeLists.txt`.
- [x] 4.4 Color labels and the "Color"/"Default" section text use `TranslationManager.translate` fallbacks (no separate file needed).

## 5. Web layout editor exposes Color

- [x] 5.1 In `src/network/shotserver_layout.cpp`, add a `WIDGET_COLORS` hex map (mirrors `Theme.qml` defaults; `default` intentionally absent) and a `READOUT_TYPES` list.
- [x] 5.2 Render an inline color `<select>` (Default + 5 named) for any selected readout chip, and a `setColor(id, value)` handler that persists `color`.
- [x] 5.3 Tint a readout chip's label with the resolved hex when a named override is set; leave `default`/unset untinted.

## 6. Verify

- [x] 6.1 Quick compile via Qt Creator MCP (build succeeded, 0 errors / 0 warnings).
- [ ] 6.2 Confirm in the running app: each of the six readouts offers Default + 5 colors; Default keeps current color (incl. Machine Status phase colors and Scale Weight state colors); a named color statically overrides text + icon in all states; two instances hold independent colors; legacy layouts (no `color`) render as Default.
