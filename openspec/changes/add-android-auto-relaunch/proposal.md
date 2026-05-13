## Why

After a self-update on Android, Decenza exits and the user has to manually find and tap the app icon to come back. Updates ship roughly weekly, so this friction is paid ~50 times per year per user. Notifications are functionally invisible on Samsung One UI tablets (where many users run Decenza), so a notification-based recovery path doesn't help; HOME-app takeover is unacceptable because users run other apps (e.g. Claude) alongside Decenza; Play In-App Updates is unavailable for sideloaded distribution. This change is an **experimental, time-boxed attempt** to see whether `SYSTEM_ALERT_WINDOW`-gated activity launch from a `MY_PACKAGE_REPLACED` receiver actually works on real user tablets. The Android docs list SAW as a Background Activity Launch exemption, but no documented real-world app uses this pattern specifically for self-update relaunch, so the outcome is unknown until we ship and measure.

### Prior attempts — read before implementing

This problem was investigated on 2026-04-19 over ~3 hours. The following commits document what was tried and what was learned. Read each before starting work:

- `a5a7ef6a feat(android): auto-launch updated app after install` — tried `startActivity` from the `PackageInstaller` status receiver. Didn't fire because the old process was killed before `STATUS_SUCCESS` arrived.
- `0db30f00 fix(android): relaunch via ACTION_MY_PACKAGE_REPLACED after self-update` — manifest receiver → `startActivity`. **BAL blocked on Samsung SM-X210 / Android 16 / target SDK 36 with logcat `result code=102`.**
- `208655c8 fix(android): post "Decenza updated — tap to open" notification after self-update` — fallback to notification. Worked technically but Samsung One UI surfaced it only as a badge count.
- `5e19c785 revert(android): remove post-install auto-launch / notification plumbing` — reverted everything. **The revert commit message is the canonical reference for what doesn't work** and should be read in full before this proposal is implemented.
- `2f2476c3 cleanup(android): remove stale comment pointing at deleted UpdateLaunchReceiver` — final cleanup.

What was **not** tried in those commits: declaring `SYSTEM_ALERT_WINDOW` as a deliberate BAL bypass. The prior work tried `setPendingIntentCreatorBackgroundActivityStartMode` + `setPendingIntentBackgroundActivityStartMode` (the documented Android 14 opt-in path) and confirmed it was silently dropped because the receiver's process had no visible window to grant BAL privileges. SAW is qualitatively different: per the Android docs' BAL exemption list it grants the **receiver's own process** BAL privileges directly, not via delegation from a different process. Whether that distinction actually survives on Samsung Android 16 is the unknown this proposal tries to resolve. **Skepticism is warranted given how many adjacent loopholes that device has already closed.**

## What Changes

- Add a new "auto-relaunch after update" capability scoped to Android.
- Declare `android.permission.SYSTEM_ALERT_WINDOW` in `android/AndroidManifest.xml.in`.
- Add a manifest-declared `BroadcastReceiver` listening for `Intent.ACTION_MY_PACKAGE_REPLACED` that calls `startActivity()` for the main launch intent (tagged with an extra so the relaunch path is detectable).
- Add an in-app one-time prompt (somewhere in Settings or surfaced after a successful download, before the install dialog) that explains the permission and routes the user to `Settings.ACTION_MANAGE_OVERLAY_PERMISSION` when they opt in.
- Detect on every app launch whether the launch came through the auto-relaunch path (via the receiver's intent extra), and log/record the result so we can tell fleet-wide whether the mechanism actually fires.
- Graceful degradation: if the permission isn't granted, the receiver still runs but the `startActivity()` call is allowed to fail silently — current "exits after update" behavior is unchanged.
- No changes to the existing `PackageInstaller` flow in `ApkInstaller.java` or `UpdateChecker.cpp`.

## Capabilities

### New Capabilities

- `android-update-relaunch`: After a Decenza self-update on Android, the app should return to the foreground automatically when the user has granted the required permission, and the system should report whether the relaunch attempt succeeded.

### Modified Capabilities

None.

## Impact

- **Android-only.** No changes to Windows, macOS, Linux, or iOS builds.
- **New Java source**: `android/src/io/github/kulitorum/decenza_de1/UpdateRelaunchReceiver.java`.
- **Manifest**: `android/AndroidManifest.xml.in` — new `<uses-permission>` and `<receiver>` entries.
- **C++**: small additions to surface the post-relaunch signal and to drive the one-time permission prompt UX. Likely lives in `src/core/updatechecker.{h,cpp}` (it already owns the update lifecycle) plus a QML entry-point for the prompt. No changes to BLE, shot, profile, or scale code.
- **Settings**: no new domain object expected. May add a single `bool` (e.g. last-relaunch-was-auto) to an existing settings domain if needed for the detection signal; default goes in the appropriate domain sub-object per the settings-architecture rules.
- **Risk profile**: experimental. May simply not work on Samsung One UI, on Android 15+, or on the user's specific tablet. The signal/log is the primary deliverable so we can decide whether to keep, revise, or revert the change. No regression risk — failure mode is identical to today's behavior.
- **Future tightening risk**: Google has been narrowing SAW privileges every major Android release (e.g., Android 15 already added a "visible overlay required" rule for the foreground-service exemption). If the activity-start exemption follows, this mechanism could stop working on a future Android version. The signal/log is designed to surface that regression rather than masking it.
- **Out of scope for this change**: Device Owner provisioning, HOME-app/launcher takeover, notification-based recovery, silent install.
