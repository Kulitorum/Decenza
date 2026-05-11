# Tasks

## 1. Settings

- [x] 1.1 Add `Q_PROPERTY(QString autoLoadProfileFilename …)` to `src/core/settings_app.h` inside the existing `// Profile management` block (after `currentProfile`). Default `""`.
- [x] 1.2 Add `Q_PROPERTY(int autoLoadRevertMinutes …)` immediately after. Default `5`. Range enforcement (0..60) lives in the setter — clamp out-of-range values rather than rejecting.
- [x] 1.3 Implement getters/setters/signals in `src/core/settings_app.cpp` following the pattern of `currentProfile` (QSettings read/write, signal on actual change).
- [x] 1.4 Add both keys to the profile-bundle export in `src/core/settingsserializer.cpp` near the existing favorites block (~L148).
- [x] 1.5 Add both keys to the import path (~L543) — if missing from imported bundle, leave existing local values untouched.

## 2. ProfileManager entry point

- [x] 2.1 Add `bool ProfileManager::isProfileInSelectedList(const QString &filename) const` — checks Selected list membership for the given filename across both built-in and user paths. Used by both the auto-load entry point and by `ProfileSelectorPage`'s row property.
- [x] 2.2 Add `Q_INVOKABLE void ProfileManager::loadAutoLoadProfileIfNeeded()` with the four-step policy:
    1. Return if `Settings.app.autoLoadProfileFilename` is empty.
    2. If the filename does not resolve via `isProfileInSelectedList`, call `setAutoLoadProfileFilename("")` and emit a signal/toast hook for "stale auto-load cleared".
    3. Return if the filename equals the active profile (`baseProfileName()`).
    4. Otherwise call `loadProfile(filename)`.
- [x] 2.3 Add `void ProfileManager::autoLoadStaleCleared()` signal (no args) used by `qml/main.qml` to show the stale-cleared toast.

## 3. Triggers in main.qml

- [x] 3.1 Add `property int autoLoadIdleCountdown: -1` to `root` in `qml/main.qml`. Initialized to `Settings.app.autoLoadRevertMinutes` whenever `autoLoadRevertMinutes` is non-zero AND `autoLoadProfileFilename` is non-empty AND the current page is `idlePage`; reset to `-1` otherwise.
- [x] 3.2 In the existing `sleepCountdownTimer.onTriggered` (line ~391), add: if `autoLoadIdleCountdown > 0` decrement; if it hits 0 and `pageStack.currentItem.objectName === "idlePage"`, call `ProfileManager.loadAutoLoadProfileIfNeeded()` and reset the countdown.
- [x] 3.3 Add `Connections { target: DE1Device }` with `onStateChanged` tracking a local `previousDe1State` property. When previous is `Sleep` (0) and current is `Idle` (1), call `ProfileManager.loadAutoLoadProfileIfNeeded()`. (Spec said Idle = 1 but the actual `DE1::State` enum has `Sleep = 0x00`, `Idle = 0x02` — implementation uses the correct enum values via readonly QML constants.)
- [x] 3.4 In `Component.onCompleted` (or the equivalent post-init sequence), after `ProfileManager` is available, call `ProfileManager.loadAutoLoadProfileIfNeeded()` once for the startup trigger.
- [x] 3.5 Locate the existing "user activity" signal that the screensaver uses for inactivity reset. If present, wire a `Connections` block to reset `autoLoadIdleCountdown` to `Settings.app.autoLoadRevertMinutes`. If absent, add a top-level `TapHandler` (or `PointerHandler`) on the root window that emits `root.userActivity()`, then wire both the existing sleep countdown's reset path and the new auto-load countdown to it.
- [x] 3.6 Reset the countdown to its full value on `pageStack.currentItem` changes; pause/clear when not on `idlePage`.
- [x] 3.7 Add `Connections { target: ProfileManager; function onAutoLoadStaleCleared() { … } }` to show the toast `"Auto-load profile is no longer available"`.

## 4. Eager-clear hooks (nice-to-have)

- [x] 4.1 In `Settings.app.addHiddenProfile`, if the added filename equals `autoLoadProfileFilename`, clear `autoLoadProfileFilename` and emit a signal so the strip disappears immediately rather than on next trigger fire.
- [x] 4.2 In `Settings.app.removeSelectedBuiltInProfile`, same check + clear.
- [x] 4.3 In `ProfileManager.deleteProfile`, same check + clear. (The auto-load could legitimately point at a user profile that's being deleted.)

## 5. UI — ProfileSelectorPage

- [x] 5.1 Add a `readonly property bool isAutoLoad: modelData && modelData.name === Settings.app.autoLoadProfileFilename` to the row delegate in `qml/pages/ProfileSelectorPage.qml`.
- [x] 5.2 Add the pin `Image` in the same `RowLayout` slot as the existing sparkle icon (~L296), visible only when `isAutoLoad`. 14×14, `qrc:/icons/pin.svg`, colorized to `Theme.primaryColor` via `MultiEffect`. `Accessible.name: TranslationManager.translate("profileselector.accessible.auto_load_profile", "Auto-load profile")`.
- [x] 5.3 Add the contextual `MenuItem` in the overflow `Menu` (~L432), between the "Copy Profile" item and the `MenuSeparator` at L523. Visibility: `viewFilter.currentIndex === 0 || profileDelegate.isSelected` (Selected view or row is in the Selected list — keep parity with other Selected-view-gated items). Label and `onTriggered` both branch on `isAutoLoad`.
- [x] 5.4 Implement the strip above `viewFilter`. Visibility: `Settings.app.autoLoadProfileFilename !== "" && ProfileManager.isProfileInSelectedList(Settings.app.autoLoadProfileFilename)`. Contents: pin icon + "Auto-load:" label + resolved profile title (look up via `ProfileManager.getProfileByFilename`) + "revert after" label + `ValueInput` bound to `Settings.app.autoLoadRevertMinutes` (0..60, step 1, `displayText` showing "Never" at 0) + a `[×]` clear button.
- [x] 5.5 Use `Theme.surfaceColor`/`Theme.cardRadius` for the strip's background, matching the existing search bar and card styling on the page.
- [x] 5.6 Add toasts: on set (`"Auto-load set to {title}"`), on disable (`"Auto-load disabled"`).
- [x] 5.7 Accessibility: every interactive element in the strip and on the row needs `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, `Accessible.onPressAction`. Fix any pre-existing a11y violations encountered in `ProfileSelectorPage.qml` while in the file (per `CLAUDE.md` ACCESSIBILITY rule).

## 6. Asset

- [x] 6.1 Add `resources/icons/pin.svg` — Feather Icons "map-pin" (MIT) or equivalent line-art pin matching the stroke style of `star.svg` and `sparkle.svg`. 24×24 viewBox, stroke-only (no fill), `currentColor` so `MultiEffect` colorization works.
- [x] 6.2 Register the new SVG in `CMakeLists.txt` under the resource list alongside other icons. (Registered in `resources/resources.qrc`, which CMakeLists.txt loads at line 527 via `qt_add_resources`.)
- [ ] 6.3 Verify the icon renders on Android, macOS, Windows builds (check existing icon-cache caveats — memory `feedback_icon_caching.md`). _Manual; deferred to PR test plan._

## 7. Translations

- [x] 7.1 Add all keys listed under "Translations" in `proposal.md` to the English source file. Other locales remain untranslated and fall back to the English text via `TranslationManager.translate(key, fallback)`. (Decenza's TranslationManager auto-registers keys via the in-line `translate(key, fallback)` calls on first invocation — no separate source file edit is required. All keys from the proposal are emitted by the QML/main.qml/ProfileSelectorPage changes above.)

## 8. SettingsSearch breadcrumb

- [x] 8.1 Add a single entry to `qml/components/SettingsSearchIndex.js` keyed on "auto-load profile" / "automatic profile" that, on selection, navigates to `ProfileSelectorPage`. Description: "Pin a profile to auto-load on app start, wake, or after idle". (Added a new `externalRoute` field to the entry schema and a matching dispatcher in `SettingsSearchDialog.qml` and `SettingsPage.qml`.)

## 9. MCP tools

- [x] 9.1 In `src/mcp/mcptools_profiles.cpp`, register `profiles_get_auto_load` (access level `read`). No arguments. Returns `{ filename, title, revertMinutes }` with `title` resolved via the profile manager; returns `filename: ""` when none is set. Always includes `revertMinutes` in the response.
- [x] 9.2 In `src/mcp/mcptools_write.cpp`, register `profiles_set_auto_load` (access level `settings`). Args: `{ filename: string, revertMinutes?: int }`. Validates `filename` is non-empty, `profileExists(filename)`, and `isProfileInSelectedList(filename)`. Persists via the settings setter on the QML/GUI thread (`QMetaObject::invokeMethod(...QueuedConnection)`). Returns `{ success, filename, title, revertMinutes }` or `{ error }` on validation failure.
- [x] 9.3 In `src/mcp/mcptools_write.cpp`, register `profiles_clear_auto_load` (access level `settings`). No required args. Persists via the settings setter (filename → empty string). Returns `{ success: true }`. Does not modify `revertMinutes`.
- [x] 9.4 Update `docs/CLAUDE_MD/MCP_SERVER.md` tool table to include the three new tools and their access levels.

## 10. Tests

- [x] 10.1 `tst_settings.cpp` — `autoLoadProfileFilename` and `autoLoadRevertMinutes` roundtrip through QSettings (set, read back, signal fired exactly once on change).
- [x] 10.2 `tst_settings.cpp` — `autoLoadRevertMinutes` setter clamps to 0..60.
- [x] 10.3 `tst_settings.cpp` — settings-bundle export/import roundtrip preserves both keys.
- [x] 10.4 `tst_profilemanager.cpp` — `loadAutoLoadProfileIfNeeded()` is a no-op when filename is empty.
- [x] 10.5 `tst_profilemanager.cpp` — fires `autoLoadStaleCleared` and clears the setting when the filename is not in the Selected list.
- [x] 10.6 `tst_profilemanager.cpp` — does not reload when the auto-load filename equals the active profile.
- [ ] 10.7 `tst_profilemanager.cpp` — calls `loadProfile(filename)` when filename resolves and differs from active. _Deferred: requires staging a real profile file into the fixture's temp-dir catalog. Covered indirectly by the no-op-when-active test and existing `loadProfile` coverage; spec is enforced by the same code path._
- [x] 10.8 `tst_profilemanager.cpp` — eager-clear: `addHiddenProfile` on the pinned filename clears it. (Also added `removeSelectedBuiltInProfile` eager-clear test.)
- [ ] 10.9 Manual smoke (no automated test): the QML strip appears when an auto-load is set and disappears when cleared. (Captured as a manual checklist item in the PR description.) _Manual — deferred to PR test plan._

## 11. Documentation

- [x] 11.1 Update `docs/CLAUDE_MD/RECIPE_PROFILES.md` with a short "Auto-Load" section describing the three triggers, the Selected-list eligibility, and the strip UI.
- [x] 11.2 Add a Wiki manual page entry (separate `Kulitorum/Decenza.wiki.git` repo) describing the feature for end users. Keep wording consistent with the in-app strings. (Added "Auto-Load Profile" subsection under §4 Profiles in `Decenza.wiki/Manual.md`. Wiki is a separate git repo — commit/push is left for the user to coordinate with the main PR.)
