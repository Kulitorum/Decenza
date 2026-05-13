## 0. Read prior attempts before writing any code

- [x] 0.1 Read `git show 5e19c785` in full — the revert commit message is the canonical reference for what doesn't work and why
- [x] 0.2 Read `git show 0db30f00` and `git show 208655c8` — the receiver and notification attempts, including the BAL `result code=102` finding
- [x] 0.3 Confirm with Jeff that the meta-question is decided: **if SAW works mechanically, is the resulting UX (one-time grant + auto-relaunch) actually better than today's (tap icon after each update)?** If not, stop and close this change without implementing.
- [x] 0.4 Recover and re-use any salvageable Java scaffolding from `UpdateLaunchReceiver.java` (deleted in `5e19c785` — recoverable via `git show 0db30f00:android/src/io/github/kulitorum/decenza_de1/UpdateLaunchReceiver.java`) and the diagnostic-file read block from `src/main.cpp` (also deleted in `5e19c785`)

## 1. Android manifest + permission plumbing

- [x] 1.1 Add `<uses-permission android:name="android.permission.SYSTEM_ALERT_WINDOW" />` to `android/AndroidManifest.xml.in`
- [ ] 1.2 Verify the manifest change flows through CMake's generation of `android/AndroidManifest.xml` and confirm both files match expectations — *requires Android build; deferred to Jeff*
- [ ] 1.3 Confirm permission appears in the built APK's manifest (use `aapt2 dump permissions` or equivalent) on at least one local build — *requires Android build; deferred to Jeff*

## 2. Java receiver

- [x] 2.1 Create `android/src/io/github/kulitorum/decenza_de1/UpdateRelaunchReceiver.java`
- [x] 2.2 Implement `onReceive()` to handle `Intent.ACTION_MY_PACKAGE_REPLACED`: build the launch intent via `getLaunchIntentForPackage()`, add `FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TOP`, attach an extra (e.g., `decenza.autorelaunch_after_update=true`), call `startActivity()` inside try/catch
- [x] 2.3 Log entry/exit with tag `DecenzaAutoRelaunch`: receiver fired, permission state, `getLaunchIntentForPackage` result, `startActivity()` success/failure
- [x] 2.4 Register the receiver in `AndroidManifest.xml.in` with `android:exported="false"` and an intent filter for `android.intent.action.MY_PACKAGE_REPLACED`
- [ ] 2.5 If practical, add a JUnit test for the intent-building logic (no Android device needed — testable as a plain Java unit test if `getLaunchIntentForPackage()` is injected or mocked) — *deferred: codebase uses Qt Test, not JUnit; the receiver is mostly Android-API plumbing that requires a real Android context; the diagnostic flag file is the empirical verification path instead*

## 3. C++ / Qt detection of auto-relaunch

- [x] 3.1 In `src/core/updatechecker.{h,cpp}`, add `Q_OS_ANDROID`-gated code to read the launching `Intent` extras (via `QNativeInterface::QAndroidApplication::context()` and JNI) on startup
- [x] 3.2 Detect the `decenza.autorelaunch_after_update` extra; if present, record a timestamp and set an "auto-relaunched this session" flag
- [x] 3.3 Emit a Qt signal / property the QML side can bind to so the diagnostic surface knows the current state
- [x] 3.4 Write a qInfo log line on detection that mirrors the Java-side `DecenzaAutoRelaunch` tag for cross-reference

## 4. Persistent diagnostic state

- [x] 4.1 Identify the appropriate Settings domain sub-object for the diagnostic flag (likely `SettingsApp`; do not put on `Settings` directly per `docs/CLAUDE_MD/SETTINGS.md`)
- [x] 4.2 Add `Q_PROPERTY` for `lastAutoRelaunchAt` (timestamp string) and `lastAutoRelaunchResult` (summary line) on `SettingsApp`. The "this launch was auto-relaunched" bool lives on `UpdateChecker.currentLaunchWasAutoRelaunch` because it's session-only state, not persisted across runs.
- [x] 4.3 Wire the receiver-detection code from §3 to update these properties
- [x] 4.4 Confirm the new property follows the recompile-blast rules (declared in the domain sub-object header, not pulled into `settings.h`)

## 5. In-app permission prompt UX

- [x] 5.1 Decide the surface — chose the existing Settings → Software Updates tab (`qml/pages/settings/SettingsUpdateTab.qml`) co-located with the other update-related toggles (`Auto-check`, `Include beta`).
- [x] 5.2 Add a QML toggle/row that reads "Auto-reopen after update" with subtext explaining the permission. Reuses `Theme` tokens, `Tr` for i18n.
- [x] 5.3 When the user taps the switch, invoke C++ JNI to launch `Settings.ACTION_MANAGE_OVERLAY_PERMISSION` with URI `package:io.github.kulitorum.decenza_de1` via `UpdateChecker.requestAutoRelaunchPermission()`
- [x] 5.4 On return to the app, re-check `Settings.canDrawOverlays()` and update the toggle state — done via `Connections { target: Qt.application; function onStateChanged(...) }` calling `refreshAutoRelaunchPermission()`
- [x] 5.5 If the toggle is off but `canDrawOverlays()` returns true (user granted externally), reflect that. Inverse: if previously on but now revoked, reflect. — The toggle is `bound` to `autoRelaunchPermissionGranted`; refresh on resume covers both directions.
- [x] 5.6 Verify all user-visible strings use `TranslationManager.translate()` / `Tr` per the QML conventions in CLAUDE.md
- [x] 5.7 Verify accessibility: `StyledSwitch` has `accessibleName` (matches the pattern used by the other switches in this file)

## 6. Diagnostic surface

- [x] 6.1 Add a small read-only diagnostic text below the toggle: "This launch was auto-reopened" (when applicable) and "Last receiver fire: <timestamp>" with the raw result summary. Visible only when there is data to show.
- [x] 6.2 Bind the diagnostic text to the C++ properties from §4
- [x] 6.3 Hide / disable the row on non-Android platforms — the entire toggle+diagnostic block has `visible: MainController.updateChecker.autoRelaunchSupported`

## 7. Graceful failure paths

- [x] 7.1 Verify the receiver does not crash when `getLaunchIntentForPackage()` returns null — handled: writes `result=no_launch_intent` to the flag file and exits cleanly
- [x] 7.2 Verify `startActivity()` failure (any exception) is caught and logged but does not crash — handled via `try { startActivity } catch (Throwable t) { ... }` in `onReceive`
- [x] 7.3 With SAW not granted, the receiver still fires but `startActivity()` is BAL-rejected. Flag file is still written (`saw=false` recorded) for diagnostics. No crash. Matches the design intent of graceful degradation.

## 8. Non-Android platform isolation

- [x] 8.1 All new C++ code is in `#ifdef Q_OS_ANDROID` guards. `UpdateChecker::autoRelaunchSupported()` returns false on non-Android; `autoRelaunchPermissionGranted()` returns false; `requestAutoRelaunchPermission()` is a no-op with a debug log; `refreshAutoRelaunchPermission()` is a no-op.
- [x] 8.2 The QML toggle+diagnostic block has `visible: MainController.updateChecker.autoRelaunchSupported` so it does not appear on iOS/Windows/macOS/Linux
- [ ] 8.3 Confirm CMake build still succeeds for at least one non-Android target (macOS or Linux) without warnings — *deferred to Jeff; build locally via Qt Creator before pushing*

## 9. Manual verification

- [ ] 9.1 Build and install on Jeff's Samsung dev tablet via `adb`
- [ ] 9.2 Without granting the permission, perform a self-update; confirm "exits to home screen" behavior is unchanged
- [ ] 9.3 Open the in-app prompt; grant `SYSTEM_ALERT_WINDOW`; confirm toggle reflects the granted state
- [ ] 9.4 Perform another self-update; observe whether the app auto-relaunches. Record outcome.
- [ ] 9.5 If it does relaunch: verify the diagnostic surface shows "This launch: auto-reopened" and a recent timestamp
- [ ] 9.6 If it does not relaunch: collect `adb logcat | grep DecenzaAutoRelaunch` output and the diagnostic surface state to determine which stage failed
- [ ] 9.7 Repeat for at least one additional tablet model if available (different Android version, different OEM)
- [ ] 9.8 Document findings in the PR description before merge

## 10. Release / followup

- [ ] 10.1 Update release notes only if the auto-relaunch is empirically confirmed working — do not advertise a feature that may not work for the user (per the "release notes: only proven user-visible" memory)
- [ ] 10.2 If empirical results are negative on Jeff's tablet, decide whether to: (a) ship anyway and let other tablets try, (b) revert the manifest and Java pieces but keep the diagnostic logging, (c) revert entirely
- [ ] 10.3 Per project workflow: open a PR (do not push to main), tag for `/pr-review-toolkit:review-pr` between open-PR and merge, then squash-merge via the `merge-pr` skill
