# Tasks

## 1. Settings

- [x] 1.1 Add `discardAbortedShots` Q_PROPERTY to `SettingsBrew` (`src/core/settings_brew.h` / `.cpp`). Type `bool`, default `true`, NOTIFY signal `discardAbortedShotsChanged`. Persist via `QSettings` under `espresso/discardAbortedShots`. Kept the split intact per `docs/CLAUDE_MD/SETTINGS.md`.
- [x] 1.2 No SETTINGS.md update needed — that file documents architecture/conventions, not a per-property registry. Confirmed with grep: no domain enumerates its individual properties there.

## 2. Classifier

- [x] 2.1 Classifier extracted to header-only `src/controllers/abortedshotclassifier.h` (namespace `decenza`) so the unit test can include it without linking the full MainController dependency graph. Constants `kAbortedDurationSec = 10.0` and `kAbortedYieldG = 5.0` are inline `constexpr` and visible in code search. Free function `decenza::isAbortedShot(durationSec, weightG)` implements the conjunction with strict `<`.
- [x] 2.2 In `MainController::endShot()`, the discard branch sits between the metadata block and the existing `m_shotHistory->saveShot(...)` call. Logs `[discard-classifier] extractionDurationSec=… finalWeightG=… verdict=… action=…` for every shot, regardless of toggle state. When discarded, emits `shotDiscarded(duration, finalWeight)`, skips both save and visualizer auto-upload, clears `m_extractionStarted`, and `return`s.
- [x] 2.3 Added `MainController::PendingDiscardedShot` struct (private, in maincontroller.h) holding the full save payload incl. a `Profile` value snapshot. Cache is cleared on `saveAbortedShotAnyway()` entry, on toast timeout (UI side) and on new-shot start (`onEspressoCycleStarted` resets `m_pendingDiscardedShot = {}`).
- [x] 2.4 `Q_INVOKABLE void MainController::saveAbortedShotAnyway()` implemented. Guards: not active → no-op; history not ready → warn+no-op; already saving → warn+no-op; data model gone → clear cache+no-op. Moves the cached payload into a local before kicking off the async save (one-shot semantics; double-tap can't re-enter). Replays both `saveShot()` and visualizer auto-upload using the snapshot's `Profile&`.

## 3. UI surface

- [x] 3.1 Existing inline `Rectangle` + `Timer` toast pattern lives in `qml/main.qml` (e.g. `flowCalToast`, `shotExportToast`). Adopted that pattern instead of building a generic component — keeps the change small.
- [x] 3.2 Added `discardedShotToast` Rectangle with a Row containing the message text + an `AccessibleButton` (`primary: true`) labelled "Save anyway". `discardedShotToastTimer` auto-dismisses after 8 s (longer than the 4 s default — user needs read+react time).
- [x] 3.3 `onShotDiscarded(durationSec, finalWeightG)` handler added to the existing MainController `Connections` block in `main.qml`. Toast appears immediately on the active page; the action calls `MainController.saveAbortedShotAnyway()` and dismisses the toast (also stops the timer to avoid re-opacity-zero racing). Announces via `AccessibilityManager.announce(..., true)` (assertive) when AT enabled.
- [x] 3.4 Translation keys added inline via `Tr` components in main.qml: `main.toast.shotDiscarded` and `main.toast.saveAnyway`. English fallback only.

## 4. Settings UI

- [x] 4.1 No `SettingsBrewTab.qml` exists — the brew domain's UI is split across other tabs. The "Prefer Weight over Volume" toggle (also a brew-domain shot policy) lives in `SettingsCalibrationTab.qml`, so the new "Discard Shots That Did Not Start" toggle was placed immediately after it for cohesion. Bound to `Settings.brew.discardAbortedShots`. Translation keys: `settings.calibration.discardAbortedShots` (label) and `settings.calibration.discardAbortedShotsDesc` (helper text).
- [x] 4.2 Used `StyledSwitch` (matches the pattern of every other toggle in `SettingsCalibrationTab.qml`) with `accessibleName` set. `StyledSwitch` is the project's accessibility-aware wrapper, so role/focus/press-action are inherited.

## 5. Tests

- [x] 5.1 `tests/tst_aborted_shot_classifier.cpp` registered in `tests/CMakeLists.txt`. **14 cases pass** via `mcp__qtcreator__run_tests`. Covers: 5 corpus positives (885, 17, 850, 836, 1) data-driven; 5 corpus negatives (890, 708, 732, 117, 868) data-driven; boundary checks at exactly 10.0 s and exactly 5.0 g (both → *kept* due to strict `<`); single-clause failures (short with real yield, long with no yield); idempotence loop. The toggle short-circuit isn't tested in this file because it's a MainController-side concern, not a classifier concern — the classifier is now a pure function with no toggle awareness.
- [N/A] 5.2 Skipping the MainController-level integration test — `MainController` has very heavy construction dependencies (DE1Device, Settings full graph, ShotDataModel, ProfileManager, ShotHistoryStorage, etc.) that no existing test file instantiates as a unit. Adding one for this single feature would require a substantial mock harness; the value-to-effort ratio is low given the discard branch is a ~10-line ifn block with deterministic behavior covered by the classifier test plus manual verification (task 7).

## 6. Observability

- [x] 6.1 `qInfo()` log line added in `MainController::onShotEnded` (every classifier eval) and in `saveAbortedShotAnyway` (replay path). Format: `[discard-classifier] extractionDurationSec=%1 finalWeightG=%2 verdict=%3 action=%4` with verdict ∈ `{aborted, kept}` and action ∈ `{discarded, saved, saved-anyway}`. Uses async logger via standard Qt logging hook.

## 7. Verification

- [ ] 7.1 Manual: with toggle on, start an espresso shot and immediately tap the stop button before the first frame runs. Verify the toast appears with the message and `Save anyway` button, and that the shot does not appear in history. Verify the toggle off case saves it normally. *(Manual on-device check — deferred to user.)*
- [ ] 7.2 Manual: tap `Save anyway` on the toast and verify the shot appears in history with the same metadata. Repeat with visualizer auto-upload enabled and verify upload runs. *(Manual on-device check — deferred to user.)*
- [x] 7.3 Desktop build clean (Qt Creator: 57 s, 0 warnings, 0 errors). Other platforms (Windows / iOS / Android) deferred — no platform-specific code in this change.
