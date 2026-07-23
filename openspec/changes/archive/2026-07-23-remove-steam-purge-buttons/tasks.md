## 1. Remove the live-view Purge button

- [x] 1.1 In `qml/pages/SteamPage.qml`, delete the `livePurgeButton` Rectangle and its
      `AccessibleTapHandler livePurgeTap` (currently ~lines 1117–1169), including the
      preceding "Purge button on the live steaming view" comment.
- [x] 1.2 Fix `steamStopButton.Keys.onBacktabPressed` (~line 1203–1212): remove the
      `livePurgeButton.visible ? livePurgeButton : ...` branch so it backtabs to
      `steamPage.lastVisiblePresetPill()` directly.
- [x] 1.3 Verify the row-level comments/visibility on `liveActionRow` still read correctly
      now that it holds only `steamStopButton` (updated the row comments and reduced its
      `visible` to `DE1Device.isHeadless`; also updated the `firstLiveActionButton()` /
      `lastLiveActionButton()` helpers that referenced `livePurgeButton`).

## 2. Remove the settings-view Purge button

- [x] 2.1 In `qml/pages/SteamPage.qml`, delete the `settingsPurgeButton` Rectangle and its
      `AccessibleTapHandler purgeTap` (currently ~lines 1651–1691), plus its now-orphaned
      trailing divider so the Duration row is not left with a leading divider.
- [x] 2.2 Re-point settings-view focus navigation that referenced `settingsPurgeButton`:
      update `durationSlider` backtab and `addPitcherButton`'s tab target so the tab/backtab
      chain closes directly between the surviving controls.

## 3. Confirm nothing dangles

- [x] 3.1 Grep `qml/pages/SteamPage.qml` for `livePurgeButton`, `settingsPurgeButton`,
      `livePurgeTap`, and `purgeTap` — confirmed zero remaining references.
- [x] 3.2 Confirm `steam.label.purge` and `steam.accessible.purge` are still referenced by
      `steamStopButton` and are therefore NOT removed.
- [x] 3.3 Confirm no C++ changes are needed — `DE1Device::requestIdle()` retains other
      callers; no slots/signals/MCP tools become unused.

## 4. Verify

- [x] 4.1 Build (done by Jeff) — clean.
- [x] 4.2 Full test suite (done by Jeff) — all tests pass.
- [x] 4.3 Manually verified on the Steam page (QML has no test harness): live steaming view shows
      no Purge button (Stop still present on headless; empty action row on GHC), settings view
      shows no Purge button, and keyboard tab/backtab traverses both views with no dead stops.

## 5. Documentation

- [x] 5.1 Updated the GitHub wiki Manual "Live Steaming Screen" section
      (`Decenza.wiki/Manual.md`): removed the **Purge** bullet, folded GHC stop-and-purge
      guidance into the **Stop** bullet, and corrected the chart-toggle note. Edit is made
      locally but NOT pushed — held per the release-timing convention pending your go-ahead.
      (Quick-Start.md and FAQ.md needed no change — they only describe stop+purge behavior,
      which is unchanged.)
