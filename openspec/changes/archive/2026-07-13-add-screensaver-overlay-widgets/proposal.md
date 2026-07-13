## Why

The screensaver currently shows exactly one optional overlay item — a clock — duplicated as four separate booleans (one per background type) each wired ad hoc into `ScreensaverPage.qml`. Users have asked for more: water level so they can tell if a refill is needed before waking the machine ([#1284](https://github.com/Kulitorum/Decenza/issues/1284)), and a way to jump to an external page (recipes, a roast log) without waking the machine ([#1310](https://github.com/Kulitorum/Decenza/issues/1310)). Both requests are small additions to the same underlying gap: the screensaver has no general-purpose overlay layer, just one hardcoded item.

## What Changes

- Replace the single per-type "Show Clock" switch in the screensaver Display settings with a chip group offering: Clock, Water Level, Shot Plan, Battery, Link Button — tap any chip to toggle it independently.
- Add three new overlay readouts (Water Level, Shot Plan, Battery), rendered as a compact icon row along the top of the screensaver, right-aligned. Each reuses the existing compact/icon rendering already built for the home screen's `StatusBar` — no new telemetry.
- Add one optional, named Link Button (Label + URL). When enabled it renders bottom-left, opens the configured URL in the system browser on tap, and does not wake the machine.
- Clock keeps its existing position (bottom-right) and existing per-type storage; it's only exposed through the new chip group instead of a lone switch. It is unavailable as a chip on the Flip Clock background (redundant there).
- All overlay anchor points (clock, top icon row, link button) drift a few pixels within their corner on a slow cycle as burn-in insurance on OLED/AMOLED tablets — no new setting.

## Capabilities

### New Capabilities
- `screensaver-overlay`: the toggleable overlay layer rendered on top of the active screensaver background — which items exist (Clock, Water Level, Shot Plan, Battery, Link Button), how each is enabled/configured per background type, where each renders, and the anti-burn-in drift behavior.

### Modified Capabilities
(none — the existing per-type clock toggle is implementation detail, not a tracked spec capability)

## Impact

- `qml/pages/settings/SettingsScreensaverTab.qml` — Display card's Show Clock switch becomes a chip group; Link Button chip reveals Label/URL fields.
- `qml/pages/ScreensaverPage.qml` — today's single hardcoded Clock `Text` element is joined by a top-right icon row (Water Level/Shot Plan/Battery) and a bottom-left Link Button; all anchor points gain slow positional drift.
- C++ `ScreensaverManager` — gains storage/properties for the three new overlay toggles and the link button's label/URL; existing `videosShowClock`/`pipesShowClock`/`attractorShowClock`/`shotMapShowClock` booleans are unchanged.
- Reused, not modified: `DE1Device::waterLevel()`/`waterLevelMm()`/`waterLevelMl()`, `BatteryManager::batteryPercent`, the existing Shot Plan widget/config (`ShotPlanItem.qml`, `ShotPlanConfig.js`), and the compact-mode rendering already shared with `StatusBar.qml`.
- Not touched: the 5-type background selector, the home-screen zone/library layout system (`SettingsLayoutTab.qml`, `LayoutEditorZone.qml`, `LibraryPanel`, `widgetCatalogTable`).
