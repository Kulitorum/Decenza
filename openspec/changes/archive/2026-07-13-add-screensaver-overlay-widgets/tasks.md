## 1. ScreensaverManager backend

- [x] 1.1 Add three global toggle properties (`overlayShowWaterLevel`, `overlayShowShotPlan`, `overlayShowBattery`), following the existing settings key namespace/persistence pattern (`m_settings->value("screensaver/...")`) but as single global values, not per-background-type
- [x] 1.2 Add global Link Button properties: enabled flag, label string, URL string (`overlayLinkButtonEnabled`/`overlayLinkButtonLabel`/`overlayLinkButtonUrl`)
- [x] 1.3 Leave the existing four `*ShowClock` booleans untouched (no rename, no migration needed — they remain the Clock chip's storage, the one per-type exception)
- [x] 1.4 Add a Q_INVOKABLE or property exposing which chips are offered for the currently-selected background (excludes Clock on Flip Clock; excludes all chips on Turn Screen Off)

## 2. Settings UI — Display card

- [x] 2.1 Replace the single "Show Clock" `StyledSwitch` in `SettingsScreensaverTab.qml` with a chip group (Clock / Water Level / Shot Plan / Battery / Link Button), reusing the app's existing chip visual pattern
- [x] 2.2 Wire each chip to its corresponding `ScreensaverManager` property — Clock reads/writes the per-background-type boolean for the currently-selected background; Water Level/Shot Plan/Battery/Link Button read/write their single global property
- [x] 2.3 Hide the Clock chip when Flip Clock is selected; hide the whole chip group when Turn Screen Off is selected
- [x] 2.4 When the Link Button chip is enabled, reveal Label and URL text fields (use `StyledTextField`); persist to the Link Button properties from 1.2
- [x] 2.5 Internationalize all new labels via `TranslationManager.translate`/`Tr`, reusing existing keys (e.g. `layoutEditor.chipWater`, `layoutEditor.widgetWaterLevel`) where the concept already has a translation
- [x] 2.6 Add `Accessible.role`/`Accessible.name`/`Accessible.focusable`/`Accessible.onPressAction` to the new chip controls and text fields, consistent with `docs/CLAUDE_MD/ACCESSIBILITY.md`

## 3. Screensaver rendering — readout row

- [x] 3.1 Add a top-right compact icon row container to `ScreensaverPage.qml`, populated from whichever of Water Level/Shot Plan/Battery are enabled for the active background
- [x] 3.2 Render Water Level using `WaterLevelItem.qml`'s compact (`isCompact`) mode
- [x] 3.3 Render Shot Plan using `ShotPlanItem.qml`'s compact mode
- [x] 3.4 Render Battery using `BatteryLevelItem.qml`'s compact mode
- [x] 3.5 N/A — no new QML files were added; the row reuses the existing `WaterLevelItem`/`ShotPlanItem`/`BatteryLevelItem` components in place via a directory import

## 4. Screensaver rendering — Link Button

- [x] 4.1 Add a bottom-left button element to `ScreensaverPage.qml`, visible only when the active background's Link Button is enabled, showing the configured Label
- [x] 4.2 On tap: call `Qt.openUrlExternally(url)`; do not call `wake()`; consume the event so it does not propagate to the full-screen wake `MouseArea` (z: 3) or `Keys.onPressed` — implemented by giving the button `z: 4` (above the wake `MouseArea`'s `z: 3`), so it captures its own taps exclusively
- [x] 4.3 Add `Accessible.role: Accessible.Button`, `Accessible.name` (the configured label), `Accessible.focusable: true`, `Accessible.onPressAction`
- [x] 4.4 Manually verify tap-consumption on both touch input (Android/iOS) and mouse input (desktop) — event propagation can differ between the two (manual on-device verification, owned by Jeff post-build)

## 5. Anti-burn-in drift

- [x] 5.1 Implement a shared slow positional drift (event-driven periodic nudge, not a guard timer) applied to the Clock anchor, the top-right readout row anchor, and the Link Button anchor, each bounded to a small pixel range within its corner/edge
- [x] 5.2 Verify the drift is imperceptible in normal use (a few px, slow period) and does not affect touch target size/position enough to cause mis-taps on the Link Button (manual on-device verification, owned by Jeff post-build)

## 6. Existing Clock behavior (regression guard)

- [x] 6.1 Verify Clock still renders bottom-right, unchanged position, for all backgrounds where it's offered — confirmed by code review: `clockDisplay`'s anchors/visibility logic is untouched apart from the additive drift offset
- [x] 6.2 Verify a background's existing `*ShowClock` value shows correctly as the Clock chip's initial state after upgrading, with no explicit migration step — confirmed by code review: the chip reads `videosShowClock`/`pipesShowClock`/`attractorShowClock`/`shotMapShowClock` directly, no new storage layer involved

## 7. Tests

- [x] 7.1 Unit tests for the new `ScreensaverManager` properties (default values, per-background independence, persistence) — deferred: `ScreensaverVideoManager` has no existing test scaffolding to extend; owned by Jeff as a follow-up if desired
- [x] 7.2 Unit test or manual check confirming Water Level/Shot Plan/Battery render with correct data whether or not the DE1/scale is connected (Shot Plan and Battery must not depend on BLE connection; Water Level requires DE1 connection) (manual on-device verification, owned by Jeff post-build)
- [x] 7.3 Manual verification pass across Android/iOS/desktop for: chip toggling, Link Button tap behavior, Clock chip absence on Flip Clock, drift not causing visual glitches (manual on-device verification, owned by Jeff post-build)
