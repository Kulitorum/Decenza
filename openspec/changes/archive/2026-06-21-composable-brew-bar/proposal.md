## Why

PR #1364 ships a "simplified home screen" as a hardcoded, full-width brew status bar (Profile / Scale / Ratio / Beans / Milk) plus setup buttons, gated behind a new `simplifiedHome` theme flag. The contents are fixed in QML and cannot be customised, and the feature bypasses the app's existing zone-driven layout system entirely. The same result — and far more — can be delivered *through* the layout editor, so the bar is composable, works identically in the in-app and web editors, requires no new mode flag, and lets every user build their own layout. Getting there needs three things the layout system lacks today: a place to put a bar above the bottom action bar, a few readout widgets, and per-zone layout/appearance options (equal-width distribution and a configurable background) that the hardcoded bar gets for free but the editor cannot currently express.

## What Changes

- Add a new **optional, location-named layout zone** `lowerMidBar` — a full-width band directly above the existing bottom action bar. It is **general-purpose** (any widget may go there, not just brew readouts), **empty by default** (existing users see no change), collapses to zero height when empty, and is hidden at runtime when the viewport is too short to fit it.
- Add **per-zone configuration** available for **all zones** (not just the new one), opened by **double-click or long-press on the zone** in the in-app editor (and an equivalent zone-options affordance in the web editor). Options include:
  - **Item distribution**: packed (current) vs. **equal-width cells** vs. spaced/justified.
  - **Alignment**: left / center / right (where applicable).
  - **Zone style**: named, theme-defined presets — `standard` (today's transparent look) and `accentBar` (matches the PR #1364 bar: accent fill, contrast bold values) — with the style components added to `Theme.qml` so users (and custom themes) can configure them; no hardcoded colors.
  - **Populate from preset**: a one-tap action to fill a zone with a built-in arrangement, including a **"Brew bar"** preset that reproduces the PR #1364 view in the new `lowerMidBar` zone (widgets + `equalWidth` + `accentBar`).
  - Reuses the existing per-zone settings storage (`layout["offsets"]`, `layout["scales"]` → add `layout["zoneOptions"]`).
- Add the **missing readout widgets** to the layout palette so a brew-style bar can be assembled from parts:
  - `profileName` — current espresso profile name.
  - `doseWeight` — measured dose (`Settings.dye.dyeBeanWeight`).
  - `milkWeight` — last measured milk weight.
  - `ratioQuickSelect` — tappable `1:X.X` pill that opens the ratio chooser (the one genuinely interactive new widget).
- Add **per-instance widget configuration**: a widget instance can be long-pressed (in-app) / opened (web) to configure how it presents its data, reusing the existing `setItemProperty`/`getItemProperties` + editor-popup mechanism used by `custom`/screensaver widgets.
- **Extend the existing `scaleWeight` widget** rather than adding a context-aware variant: a per-instance "data mode" lets each Scale instance show gross weight, net beans (minus dose-cup tare), net milk (minus pitcher weight), or context-aware.
- Register all new widgets (4 places) and the new zone (zone registries) as required.
- After this lands, PR #1364's `simplifiedHome` flag and hardcoded `brewStatusBar` are unnecessary; the simplified home can ship as an optional **default layout** that pre-fills `lowerMidBar`, or be left entirely to the user.

## Capabilities

### New Capabilities
- `layout-lower-mid-bar-zone`: An optional, general-purpose, location-named (`lowerMidBar`) layout zone above the bottom action bar — empty by default, collapses when empty, hidden when the viewport is too short.
- `layout-zone-configuration`: Per-zone layout and appearance options for all zones, opened via double-click/long-press on the zone in the editor — item distribution (incl. equal-width), alignment, and a theme-driven background.
- `layout-brew-widgets`: New layout palette widgets — current profile name, measured dose, measured milk, and an interactive ratio quick-select pill.
- `layout-widget-instance-config`: Per-instance configuration of layout widgets via the in-app (long-press) and web editors, including a configurable data mode for the existing `scaleWeight` widget (gross / net-beans / net-milk / context-aware).

### Modified Capabilities
<!-- No existing OpenSpec capability covers the layout/zone system; all behaviour here is net-new. -->

## Impact

- **QML widgets**: new `ProfileNameItem.qml`, `DoseWeightItem.qml`, `MilkWeightItem.qml`, `RatioQuickSelectItem.qml`; modified `ScaleWeightItem.qml` (per-instance data mode).
- **QML zone/render**: `IdlePage.qml` (render `lowerMidBar`, height-gated); `LayoutCenterZone.qml` (generalise the distribution/alignment behaviour into a per-zone option; apply background); `LayoutItemDelegate.qml` (switch entries).
- **QML editor**: `SettingsLayoutTab.qml` + `LayoutEditorZone.qml` (zone-options gesture/panel, palette entries, per-instance editor for `scaleWeight`/new widgets, register `lowerMidBar`).
- **Theme**: `Theme.qml` — add named zone-style presets (`standard`, `accentBar`) as configurable theme components, each bundling background + label/value text + emphasis with a contrast helper, so zone appearance stays theme-consistent across light/dark/custom palettes and custom themes can override them.
- **C++**: `settings_network.{h,cpp}` (default layout JSON adds empty `lowerMidBar` zone + migration; add `getZoneOption`/`setZoneOption` over a `layout["zoneOptions"]` map, mirroring offsets/scales).
- **Web editor**: `src/network/shotserver_layout.cpp` (new widget list entries, `lowerMidBar` zone entry, zone-options controls + endpoint, extend `openEditor` for `scaleWeight`).
- **PR #1364**: superseded in approach — `simplifiedHome` (`settings_theme.{h,cpp}`) and the hardcoded `brewStatusBar` dropped; `RatioPresetDialog` and the three `ratioPreset` brew settings retained (reused by `ratioQuickSelect`).
- **No change for existing users**: `lowerMidBar` ships empty and all zone options default to current behaviour, so default layouts are byte-for-byte unchanged.
