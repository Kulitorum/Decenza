# Tasks

## 1. Settings

- [x] 1.1 Add `discardAbortedShots` Q_PROPERTY to `SettingsBrew` (`src/core/settings_brew.h` / `.cpp`). Type `bool`, default `true`, NOTIFY signal `discardAbortedShotsChanged`. Persist via `QSettings` under `espresso/discardAbortedShots`. Kept the split intact per `docs/CLAUDE_MD/SETTINGS.md`.
- [x] 1.2 No SETTINGS.md update needed — that file documents architecture/conventions, not a per-property registry. Confirmed with grep: no domain enumerates its individual properties there.

## 2. Classifier

- [x] 2.1 Classifier extracted to header-only `src/controllers/abortedshotclassifier.h` (namespace `decenza`) so the unit test can include it without linking the full MainController dependency graph. Constants `kAbortedDurationSec = 10.0` and `kAbortedYieldG = 5.0` are inline `constexpr` and visible in code search. Free function `decenza::isAbortedShot(durationSec, weightG)` implements the conjunction with strict `<`.
- [x] 2.2 In `MainController::endShot()`, the discard branch sits between the metadata block and the existing `m_shotHistory->saveShot(...)` call. Logs `[discard-classifier] extractionDurationSec=… finalWeightG=… verdict=… action=…` for every shot, regardless of toggle state. When discarded, emits `shotDiscarded(duration, finalWeight)`, skips both save and visualizer auto-upload, clears `m_extractionStarted`, and `return`s. **The `m_pendingShotEpoch` and `m_pendingDebugLog` member writes were moved to *after* the discard branch** so a dropped shot can't corrupt pending-shot state from a prior unflushed shot (PR-review finding #3).
- [N/A] 2.3 No payload cache — the "Save anyway" path was scoped out (see proposal). A discarded shot is intentionally irrecoverable.
- [N/A] 2.4 No `saveAbortedShotAnyway()` Q_INVOKABLE — see 2.3.

## 3. UI surface

- [x] 3.1 Existing inline `Rectangle` + `Timer` toast pattern lives in `qml/main.qml` (e.g. `flowCalToast`, `shotExportToast`). Adopted that pattern instead of building a generic component — keeps the change small.
- [x] 3.2 Added `discardedShotToast` Rectangle with a centered Text label. `discardedShotToastTimer` auto-dismisses after 4 s (matches the other toast timers).
- [x] 3.3 `onShotDiscarded(durationSec, finalWeightG)` handler added to the existing MainController `Connections` block in `main.qml`. Toast appears immediately on the active page. Announces via `AccessibilityManager.announce(..., true)` (assertive) when AT enabled.
- [x] 3.4 Translation key `main.toast.shotDiscarded` added inline via `Tr` component in main.qml. English fallback only.

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

## 8. Post-review changes (PR #912 review feedback)

- [x] 8.1 Dropped the "Save anyway" recovery path entirely. A discarded shot is intentionally not recoverable — the toggle is the right escape hatch. Removed: `saveAbortedShotAnyway()` Q_INVOKABLE, `PendingDiscardedShot` struct + member, the cache writes in the discard branch, the cache reset in `onEspressoCycleStarted`, the toast's action button, and the corresponding spec scenarios.
- [x] 8.2 Moved `m_pendingShotEpoch` / `m_pendingDebugLog` member assignments below the discard branch so a dropped shot can't corrupt pending-shot state belonging to a prior unflushed shot (review finding #3).
- [x] 8.3 Switched test main macro from `QTEST_APPLESS_MAIN` to `QTEST_GUILESS_MAIN` to match `docs/CLAUDE_MD/TESTING.md` and existing pure-function tests (review below-threshold finding).
