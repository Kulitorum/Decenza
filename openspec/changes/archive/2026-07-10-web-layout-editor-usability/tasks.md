# Tasks — Web Layout Editor Usability & QML Parity

## 1. Page layout & responsiveness (D1)

- [x] 1.1 Restructure `.main-wrapper` in `shotserver_layout.cpp` to a grid: full-width instructions row on top; `zones | right column` below. Kill the flex-stretching of the instruction `<p>`.
- [x] 1.2 Add a right column container holding the (new) preview pane above the library panel, mirroring the in-app editor's split.
- [x] 1.3 Add a ~1100px breakpoint that stacks the right column below the zones; verify no panel overlap at 1024×768 and sane rendering at 1400/1500px.

## 2. Options affordance & editor lifecycle (D2)

- [x] 2.1 Make the chip gear an interactive control: `stopPropagation()` click handler that selects the chip and opens the type-appropriate editor (custom → `openEditor`, bespoke/screensaver/shotPlan/lastShot → `openScreensaverEditor`, readouts → new `openReadoutOptions`), regardless of prior selection state.
- [x] 2.2 Remove the implicit editor-opening from `chipClick()` (click = select/deselect only, matching in-app).
- [x] 2.3 Close any open instance editor when its chip is deselected, when another chip is selected, and on remove (extend the existing removeItem cleanup to cover deselect).
- [x] 2.4 Update the web instruction text: drag to reorder, click to select, gear to open options; keep wording consistent with the in-app `settings.layout.instructions` string.

## 3. Labeled readout options editor (D3)

- [x] 3.1 Add a readout mode to the editor-panel card: sections generated from `WIDGET_CAPABILITIES[type]` in schema order (dataMode, displayMode, showRatio, color as applicable).
- [x] 3.2 Mirror `ReadoutOptionsPopup.qml` wording: section headers ("Scale data mode", "Display", "Color"), descriptive choice labels ("Net beans (minus dose tare)", "Context-aware (milk while steaming, else beans)", "Expected output (target weight)", "Value only"/"Icon + value"), and the show-ratio hint. Add keep-in-sync comments in both files pointing at each other.
- [x] 3.3 Persist via the existing `/api/layout/item` auto-save path (200ms debounce), injecting `WIDGET_DISPLAY_DEFAULTS` for absent displayMode.
- [x] 3.4 Remove the inline `.chip-mode` selects (dataMode/showRatio/displayMode/color and the sleep allowQuit/showIcon pair) — sleep options move into the same editor-panel pattern.

## 4. Stable chips & remove confirmation (D5, D7)

- [x] 4.1 Render the × on every chip, faint by default, full opacity on chip hover or selection; verify selection no longer resizes the chip or reflows the zone.
- [x] 4.2 Add `configured` per item to the `/api/layout` GET payload from `SettingsNetwork::itemIsConfigured()`.
- [x] 4.3 Gate web remove on `confirm("Remove this widget and its settings?")` when `configured`; direct removal otherwise.

## 5. Zone control labeling (D7)

- [x] 5.1 Add `title` tooltips + `aria-label`s to the zone offset (▲/▼), scale (−/+) buttons and their value readouts ("Vertical offset", "Zone scale").

## 6. Live web preview (D4)

- [x] 6.1 Add a preview pane rendering a scaled 960×600 home-screen mock from `layoutData`: status bar, top bars, three center zones, lower-mid bar, bottom bars, positioned as on the device.
- [x] 6.2 Honor zone options in the preview: distribution (packed/equalWidth/spaced), alignment, style background, per-zone offset and scale, hidden lower-mid bar when empty.
- [x] 6.3 Render items as labeled mini-chips using `DISPLAY_NAMES`/`WIDGET_COLORS` (custom items show emoji + text; spacers/separators render as gaps/dividers).
- [x] 6.4 Re-render the preview from `loadLayout()` so every mutation updates it live; verify add/remove/reorder/option changes all reflect without reload.

## 7. Settings-access guard (D6)

- [x] 7.1 Add `Q_INVOKABLE void SettingsNetwork::ensureSettingsAccessible()` (port of the QML scan-and-add logic).
- [x] 7.2 Call it after web layout mutations that can remove Settings access (remove, zone clear/reset, layout reset, custom-item action edits).
- [x] 7.3 Switch `SettingsLayoutTab.qml` `ensureSettingsAccessible()` to call the shared C++ invokable; delete the QML copy.
- [x] 7.4 Unit test: removing the last settings widget via the API path restores one (see TESTING.md conventions before writing).

## 8. Verification

- [x] 8.1 Browser pass at 1500px and 1024px: layout, gear-open on every click, editor closes on deselect/remove, readout editor labels match in-app, remove-confirm on configured widgets, preview matches tablet after edits.
- [x] 8.2 In-app pass: layout editor still works (guard refactor), preview unaffected.
- [x] 8.3 Run the test suite via Qt Creator MCP (`run_tests`), fix failures.
