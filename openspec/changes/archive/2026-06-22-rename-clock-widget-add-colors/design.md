## Context

The Clock widget (#1375) is a per-instance layout widget identified by `type: "clock"`. It already supports a per-instance `displayMode` (`text` / `icon`) routed through `DisplayModeEditorPopup` and persisted via `Settings.network.setItemProperty`. Its current implementation hard-codes `Theme.textColor` for both the time text and the icon in compact and full modes (`qml/components/layout/items/ClockItem.qml`).

Two inconsistencies/limitations drive this change:
1. The native picker label says "Clock" (`LayoutEditorZone.qml:494`) and the web picker says "Clock" (`shotserver_layout.cpp:2421`), while the chip label (`LayoutEditorZone.qml:694`) and web display-name map (`shotserver_layout.cpp:2450`) already say "Time".
2. The widget has no color choice.

Relevant existing patterns:
- Per-instance options route through `SettingsLayoutTab.openCustomEditor()`, which dispatches by `type` to a popup (`DisplayModeEditorPopup`, `ScaleWeightEditorPopup`, etc.). Clock currently shares `DisplayModeEditorPopup` with `machineStatus`/`temperature`/`steamTemperature`/`waterLevel`.
- The web editor renders inline `<select>` chips for selected configurable widgets in `shotserver_layout.cpp` (~line 2707) and persists via JS handlers that POST property updates.
- `Settings.network.typeHasOptions()` already includes `"clock"`, so the gear/options affordance is shown.

## Goals / Non-Goals

**Goals:**
- Rename the user-visible picker label for the clock widget to "Time" in both editors, leaving the `clock` type id untouched.
- Add a per-instance `color` property (`white` default, plus `green`/`red`/`blue`/`orange`) that tints the clock's text and icon, mapped to existing theme colors.
- Expose the color choice in both the native and web per-instance editors, persisted in layout JSON.
- Keep existing layouts working with zero migration (unset = White).

**Non-Goals:**
- No color option for other readout widgets (temperature, machineStatus, etc.) — scoped to the clock only, per the request.
- No free-form/custom hex color picker — a fixed five-value palette only.
- No change to the 12/24-hour formatting, the display-mode behaviour, or the widget's zones.

## Decisions

### Color palette maps to semantic theme colors, not raw hex
`white` → `Theme.textColor`, `green` → `Theme.pressureColor` (#18c37e), `red` → `Theme.temperatureColor` (#e73249), `blue` → `Theme.flowColor` (#4e85f4), `orange` → `Theme.warningColor` (#ffaa00).

Rationale: the user asked for "the other colors that widgets on the page use." The home-screen graph already draws pressure (green), flow (blue), and temperature (red) in these theme colors, and warning/alerts use the orange. Reusing the theme tokens keeps the clock visually consistent with the rest of the page and makes custom themes recolor the clock automatically. *Alternative considered:* hard-coded hex constants — rejected because they would drift from the theme and ignore user color customization.

The mapping lives in one place. In QML, a small `colorFor(name)` helper in `ClockItem.qml` returns the theme color; the web editor needs its own hex map for the live preview (it renders the home screen with resolved colors), so the same five hex values are mirrored once in `shotserver_layout.cpp`.

### Store the choice under a `color` property key, default-absent = white
Persisted via `Settings.network.setItemProperty(itemId, "color", value)` and read as `modelData.color` in `ClockItem.qml`. Absence → `white`. `color` is not in the QML reserved-name list for model data, and `ClockItem` is an `Item` (no `color` property of its own), so there is no binding conflict.

*Alternative considered:* `colorMode`/`tint` — `color` is the most direct and reads cleanly in both the JSON and the editor; chosen for clarity.

### Give the clock its own editor popup instead of overloading DisplayModeEditorPopup
`DisplayModeEditorPopup` is shared by four other readout widgets that should NOT get a color option. Rather than branch inside it, add a dedicated `ClockEditorPopup.qml` (modeled on `ScaleWeightEditorPopup`, which already combines two choices — `dataMode` + `displayMode`) presenting **Display** (text/icon) and **Color** (5 swatches). Route `type === "clock"` to it in `SettingsLayoutTab.openCustomEditor()`, removing `clock` from the `DisplayModeEditorPopup` branch.

*Alternative considered:* add an optional color row to `DisplayModeEditorPopup` gated on type — rejected; it couples five widgets' editor to one widget's feature and muddies a currently-simple shared popup.

### Web editor mirrors the native flow
In `shotserver_layout.cpp`: change the picker catalog label to "Time"; add an inline color `<select>` for a selected clock chip next to its existing display-mode `<select>` (~line 2707); add a `setClockColor` JS handler that persists `color`; and apply the resolved hex when rendering the clock preview.

## Risks / Trade-offs

- [Web/native palette drift] The five hex values are defined in both QML (via theme tokens) and the web editor (literal hex). → Mirror the exact default theme hex values in `shotserver_layout.cpp` and add a short comment cross-referencing `Theme.qml`; the web editor already mirrors other theme colors, so this follows existing precedent.
- [Custom-theme divergence in web preview] If the user has a custom theme, the web preview's literal hex won't reflect it, while the native widget will. → Acceptable: the web editor is a layout-arrangement tool, not a pixel-accurate theme preview, and it already uses static colors elsewhere.
- [Reserved-name binding] Using `color` as the property key on a model object consumed by `ClockItem`. → `ClockItem` is an `Item`; verified there is no `color` property to shadow. Read defensively as `(modelData && modelData.color) ? ... : "white"`.

## Migration Plan

No data migration. The new `color` key is optional; existing `clock` items without it render White (current behaviour). The change is additive and backward compatible. Rollback is a straight revert — older builds simply ignore an unknown `color` key on the item.

## Open Questions

- None blocking. (Label "White" is used for the default swatch even though a custom theme's text color may not literally be white; this matches how users refer to the current default and keeps the palette labels simple.)
