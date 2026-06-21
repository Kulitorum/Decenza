## Context

This applies the #1368 rework pattern to the top status bar, but lighter — because nothing about the bar's geometry changes. The top bar's height is fixed by its host (`main.qml` → `height: Theme.statusBarHeight`), and PR #1362's "compact" look is a content *style* (icon-led, condensed) within that same-size bar, not a smaller zone. Every element of that bar already exists as a layout widget: `machineStatus`, `temperature`, `steamTemperature`, `scaleWeight`, `batteryLevel`, `sleep`. Icon assets already exist in `resources/icons/`.

`StatusBar.qml` renders the `statusBar` zone with its own `RowLayout` + `Repeater` over `LayoutItemDelegate`, which already (a) reads the configurable `statusBar` zone items and (b) gives `Layout.fillWidth` to `spacer` items. So a populated `statusBar` zone with spacers renders centred content with the **current** renderer — no refactor needed.

Reused machinery from #1368: the per-instance property + long-press editor pattern (`setItemProperty`/`getItemProperties`, as used by `ScaleWeightItem.dataMode`), the populate-from-preset path (C++ + web + `ZoneOptionsPopup`), and `LayoutItemDelegate` already binding `modelData` so widgets can read their own stored properties.

`compactStatusBar`/the hardcoded bar are **not on `main`**, so this builds on `main` and supersedes #1362 (nothing to remove).

## Goals / Non-Goals

**Goals:**
- Deliver the icon-led status bar through the layout editor — composable, no flag.
- Make the icon capability a **per-instance display option** on existing widgets, usable in **any** zone (mixed zones included), not gated by a zone style.
- Let the Sleep widget's quit option be removed per instance.
- One-tap "Compact status bar" via a populate preset.
- Keep every default unchanged.

**Non-Goals:**
- The `compactStatusBar` flag and hardcoded bar — superseded.
- A `compact` zone style — replaced by per-instance display modes.
- New layout widgets or new icon assets — none; this only adds options to existing items.

**In scope (added during implementation):**
- Refactor `StatusBar.qml` to render via `LayoutBarZone` so the `statusBar` zone honors the per-zone options (distribution / alignment / style) the editor offers — otherwise those options are dead for the top bar (it was the last zone with a special-cased renderer). `LayoutBarZone` packed mode is extended to fill the row when a spacer is present, preserving the default status-bar layout.

## Decisions

### D1: Per-instance `displayMode` on the four readout widgets

Add a `displayMode` property (`text` default, `icon`) to `MachineStatusItem`, `TemperatureItem`, `SteamTemperatureItem`, `ScaleWeightItem`, read from `modelData` (the widget already receives `modelData` via `LayoutItemDelegate`, or declares it where needed — `ScaleWeightItem` already does for `dataMode`). In `icon` mode the widget renders its icon ahead of the value, tinted to the surrounding text color (so it works on any background, including a `zoneTextColor`-styled zone). Default `text` = today's exact rendering.

Because the mode lives on the item instance and is read from stored properties, the icon form works in **any** zone — that's the user's "usable in other mixed zones" requirement, and it's strictly more flexible than a zone-style trigger.

**Alternative considered:** zone-style-driven icons (a `compact` style flips the whole bar). Rejected per user decision — per-instance is more flexible and reusable, and the geometry doesn't change so there's no need for a zone-level switch.

### D2: Long-press editor for the display options

Reuse the per-instance editor path. Register `machineStatus`/`temperature`/`steamTemperature`/`scaleWeight` as configurable so a long-press in the editor opens a display-mode editor. A shared `DisplayModeEditorPopup` (text/icon toggle) serves the three pure readouts; `ScaleWeightEditorPopup` gains a `displayMode` row alongside its existing `dataMode`. The web editor adds an inline `displayMode` selector on those chips (mirroring the scale `dataMode` selector).

### D3: Per-instance Sleep `allowQuit`

`SleepItem` reads `allowQuit` (default true) from `modelData`; when false, the `onPressAndHold`/`onAccessibleLongPressed` quit paths are disabled and the "long-press to quit" accessibility hint is dropped. Long-press in the editor opens a small `SleepEditorPopup` (single toggle); the web editor gets an inline toggle on the sleep chip. The explicit `quit` widget (`QuitItem`) remains the deliberate quit control.

### D4: "Compact status bar" populate preset (no renderer change)

Extend the populate path with a `compactStatusBar` preset: `machineStatus` · `temperature` · `steamTemperature` · `scaleWeight` · `batteryLevel` (each `displayMode: icon`) with spacer · `sleep` · spacer to centre Sleep. Offered for `statusBar`. The existing `StatusBar.qml` Repeater renders it (spacers already get `Layout.fillWidth`), so **no `StatusBar.qml` change** is required.

## Risks / Trade-offs

- **[Two per-instance options on `scaleWeight`]** It now has `dataMode` + `displayMode` → Mitigation: one editor popup with both rows; both default to current behaviour.
- **[Icon rendering in `full` (center-zone) mode]** The widgets have compact and full render paths → Mitigation: apply the icon in both paths (or at least the compact/bar path the bar uses), defaulting off so center zones are unaffected unless opted in.
- **[Centred Sleep relies on spacers]** The preset must include spacers both sides → Mitigation: the preset writes them; the existing Repeater already fills spacer width.
- **[Editor parity]** display-mode + sleep toggle + preset must exist in both editors → Mitigation: shared `setItemProperty` storage; parity is an explicit task.

## Migration Plan

1. Add `displayMode` to the four widgets (default `text`, no visible change).
2. Add the editors (display-mode popup, sleep popup, web inline selectors) + register long-press routing.
3. Add `allowQuit` to `SleepItem`.
4. Add the "Compact status bar" populate preset (C++ + web + `ZoneOptionsPopup`).
5. Supersede #1362 (build on `main`). Optionally ship a compact default `statusBar` config later (data, not code).
6. Rollback: the new properties/preset are inert if unused; revert the widget/editor edits.

## Open Questions

- Should `scaleWeight` get an icon in `icon` mode, or is its value rendering enough? (Lean: include the `scale.svg` icon for consistency, default `text`.)
- Ship the "Compact status bar" as an alternate **default** `statusBar` config too, or only via the populate action? (Recommendation: populate now; optional default later.)
