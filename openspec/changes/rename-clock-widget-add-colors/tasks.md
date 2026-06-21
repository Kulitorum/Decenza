## 1. Rename picker label to "Time"

- [x] 1.1 In `qml/pages/settings/LayoutEditorZone.qml:494`, change the clock catalog entry label from `"Clock"` to `"Time"` (reuse the existing chip key `layoutEditor.chipTime`, or add `layoutEditor.widgetTime` / repoint `layoutEditor.widgetClock` to fallback "Time"). Verify the chip label at line 694 still reads "Time".
- [x] 1.2 In `src/network/shotserver_layout.cpp:2421`, change the clock `WIDGET_TYPES` catalog entry label from `"Clock"` to `"Time"`. Confirm the `DISPLAY_NAMES` map (line 2450) already shows `clock:"Time"`.

## 2. Clock widget renders the per-instance color

- [x] 2.1 In `qml/components/layout/items/ClockItem.qml`, add `readonly property string clockColor: (modelData && modelData.color) ? modelData.color : "white"` and a `function colorFor(name)` returning `white`→`Theme.textColor`, `green`→`Theme.pressureColor`, `red`→`Theme.temperatureColor`, `blue`→`Theme.flowColor`, `orange`→`Theme.warningColor` (default `Theme.textColor`).
- [x] 2.2 Replace the hard-coded `color: Theme.textColor` on the compact-mode icon and time text (lines ~62, ~68) and the full-mode icon and time text (lines ~80, ~99) with `color: colorFor(root.clockColor)`. Leave the "Time" caption (`textSecondaryColor`) unchanged.

## 3. Native per-instance editor exposes Color

- [x] 3.1 Create `qml/components/layout/ClockEditorPopup.qml` (modeled on `ScaleWeightEditorPopup.qml`) with a Display section (text/icon) and a Color section (5 swatches: White, Green, Red, Blue, Orange), each persisting via `Settings.network.setItemProperty(itemId, "displayMode"/"color", value)`. Add `openForItem(id, displayMode, color)`.
- [x] 3.2 Add `ClockEditorPopup.qml` to the `qt_add_qml_module` file list in `CMakeLists.txt`.
- [x] 3.3 In `qml/pages/settings/SettingsLayoutTab.qml` `openCustomEditor()`, remove `clock` from the `DisplayModeEditorPopup` branch and add a `type === "clock"` branch that calls `clockEditorPopup.openForItem(itemId, props.displayMode || "", props.color || "")`. Declare the `clockEditorPopup` instance and include it in `closeOptionEditors()`.
- [x] 3.4 Add translation keys for the color labels (e.g. `layoutEditor.colorWhite/Green/Red/Blue/Orange`) and the editor "Color" section header.

## 4. Web layout editor exposes Color

- [x] 4.1 In `src/network/shotserver_layout.cpp` (~line 2707, where the clock display-mode `<select>` is rendered for a selected chip), add an inline color `<select>` with options White/Green/Red/Blue/Orange bound to `item.color || "white"`.
- [x] 4.2 Add a `setClockColor(id, value)` JS handler that persists the `color` property for the item (mirroring the existing display-mode persistence path).
- [x] 4.3 Apply the resolved color when rendering the clock preview, using a hex map mirroring the `Theme.qml` defaults (white #ffffff, green #18c37e, red #e73249, blue #4e85f4, orange #ffaa00) with a comment cross-referencing `Theme.qml`.

## 5. Verify

- [x] 5.1 Quick compile via Qt Creator MCP (match the worktree path, build).
- [ ] 5.2 Confirm: picker shows "Time" in both editors; adding a Time widget defaults to White; choosing Green/Red/Blue/Orange tints both text and icon; two clock instances hold independent colors; an existing layout with a `clock` item (no `color`) still renders White.
