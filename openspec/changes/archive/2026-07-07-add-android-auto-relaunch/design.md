## Context

Decenza distributes Android builds as sideloaded APKs (no Play Store). The current update flow uses `PackageInstaller` session API (see `android/src/io/github/kulitorum/decenza_de1/ApkInstaller.java`): the user accepts an in-app prompt, then accepts the system install dialog, then the system kills our process and replaces the APK. After install completes the user lands at the system home screen and must manually re-open Decenza.

Android tightened Background Activity Launch (BAL) rules in API 29 and again in 31, 33, 34, and 35. The set of mechanisms that allow an app to start an activity from the background is small and getting smaller:

| Mechanism | Available to Decenza? |
|---|---|
| Play In-App Updates | No — sideloaded |
| Notification PendingIntent | Yes, but Samsung One UI hides notifications behind a manual swipe; users have stated it's not useful |
| HOME app (`CATEGORY_HOME`) | Rejected — users run Claude and other apps alongside Decenza |
| Device Owner mode | Out of scope — provisioning cost is high |
| `SYSTEM_ALERT_WINDOW` permission | Documented as a BAL exemption; **untested for this specific use case** |
| `MY_PACKAGE_REPLACED` receiver alone | Documented as NOT a BAL exemption — fails silently |

This change attempts the `SYSTEM_ALERT_WINDOW` path. It is **experimental**: theory says it should work, but no documented real-world app uses SAW specifically for self-update relaunch, and Google has narrowed SAW privileges every recent release.

**Prior empirical findings (commits a5a7ef6a / 0db30f00 / 208655c8 / 5e19c785, 2026-04-19):**

The same problem was attacked three weeks before this proposal and reverted after ~3 hours. The reverted code is gone but its lessons must inform this attempt:

- `MY_PACKAGE_REPLACED` → `startActivity()` from a manifest receiver was confirmed BAL-blocked on Samsung SM-X210 / Android 16 / target SDK 36 with logcat `result code=102`.
- `PendingIntent` with both `setPendingIntentCreatorBackgroundActivityStartMode(MODE_BACKGROUND_ACTIVITY_START_ALLOWED)` and `setPendingIntentBackgroundActivityStartMode(MODE_BACKGROUND_ACTIVITY_START_ALLOWED)` was also blocked. Reason: the creator opt-in was silently dropped because the receiver's process had no visible window — there was no BAL privilege to delegate.
- Notification fallback worked technically but Samsung One UI surfaced it only as a badge count → not UX-better than today's exit-to-launcher.
- `PackageInstaller.SessionParams` has no post-install UI options.
- Google issue 138144278 confirms Android's intended auto-posted install-complete notification has been broken since launch.

**What this proposal does differently:** declare `SYSTEM_ALERT_WINDOW` so that when the receiver runs, the receiver's process itself holds a documented BAL exemption (#13 in the Android BAL doc), not a delegated permission from a dead process. Whether that distinction is enough to bypass Samsung One UI's BAL enforcement is the open empirical question.

## Goals / Non-Goals

**Goals:**

- After a self-update on a user's Android tablet, the app returns to the foreground without the user finding and tapping the launcher icon.
- We can tell, from logs/state on the next launch, whether the relaunch path fired or whether it was suppressed.
- If `SYSTEM_ALERT_WINDOW` is not granted, current behaviour is preserved (app exits after update; user re-opens manually).
- Permission request is opt-in and surfaced with context explaining why.

**Non-Goals:**

- Silent update with no install dialog (would require Device Owner).
- Auto-relaunch after a fresh install (Android's stopped-state forbids this, and the receiver is by definition not registered yet).
- Auto-relaunch after a reboot (separate `BOOT_COMPLETED` discussion; not addressed here).
- Visible overlay UI (chathead, floating timer, etc.) — even though the permission permits this.
- iOS, Windows, macOS, Linux update behavior — unchanged.
- Changes to `ApkInstaller.java` session flow or the `UpdateChecker` install state machine.

## Decisions

### D1: Use `SYSTEM_ALERT_WINDOW` as the BAL bypass, not `MY_PACKAGE_REPLACED` alone

The official Android BAL exemption list includes "The app is granted the `SYSTEM_ALERT_WINDOW` permission by the user" but does NOT include `MY_PACKAGE_REPLACED` (only `ACTION_NEW_OUTGOING_CALL` and `SECRET_CODE_ACTION` are listed as system broadcasts where activity launch is permitted). A receiver-only approach would compile and run but the `startActivity()` call would be silently dropped by the OS on Android 10+. The prior attempt in commit `0db30f00` confirmed this empirically on Samsung Android 16.

**Alternatives considered:**

- *`MY_PACKAGE_REPLACED` receiver only.* **Already tried (commit `0db30f00`) and confirmed BAL-blocked on Samsung Android 16.** Rejected.
- *`PendingIntent` with `setPendingIntent{Creator,}BackgroundActivityStartMode(MODE_BACKGROUND_ACTIVITY_START_ALLOWED)`.* **Already tried (commit `208655c8`) and confirmed blocked — creator opt-in silently dropped.** Rejected.
- *Foreground service exemption.* Rejected: Android docs explicitly state "For the purpose of starting activities, an app running a foreground service is considered to be in the background."
- *Full-screen-intent notification.* Rejected: still requires a notification that Samsung One UI hides.
- *Notification with launch PendingIntent.* **Already tried (commit `208655c8`) and reverted (`5e19c785`) because Samsung One UI surfaced it only as a badge count.** Not UX-better than current state.
- *`SYSTEM_ALERT_WINDOW` + receiver.* **Not previously tried.** Selected — the only remaining documented BAL path that prior work did not eliminate.
- *Device Owner.* Rejected as out of scope (separate, larger change).

### D2: Permission request is opt-in with explanatory copy

`SYSTEM_ALERT_WINDOW` is a "special permission" requested by sending the user to `Settings.ACTION_MANAGE_OVERLAY_PERMISSION`. This is heavier UX than a normal runtime permission. The prompt should:

- Appear once, surfaced from an obvious context (e.g., from the Updates section of Settings, or as a one-time offer the first time a successful auto-update could have happened).
- Explain *why* — "Lets Decenza reopen automatically after each weekly update."
- Be dismissible without penalty (the app continues to work; updates still install; user just re-opens manually).
- Be re-discoverable through the Settings page so a user who said "no" can change their mind.

**Alternatives considered:**

- *Auto-prompt on first launch.* Rejected — too aggressive for a permission the user might not need.
- *Silent declare-only.* Won't work — Android ≥ 6.0 requires the runtime grant flow.

### D3: Track and log whether the relaunch path actually fired

Silent failure is the primary risk. The Android docs say SAW grants BAL privileges, but no app appears to use it specifically for self-update relaunch, and Samsung One UI has historically introduced surprise restrictions. We must be able to answer "did the receiver fire?" and "did the activity launch succeed?" from inside the app after the next launch.

The mechanism:

1. The receiver tags the launch `Intent` with an extra such as `decenza.autorelaunch_after_update=true`.
2. On `Activity.onCreate()` / Qt JNI startup, we read this extra. If present, we record the event (log line and/or a persisted timestamp).
3. The receiver also logs its own entry/exit (with success/failure of `startActivity()`) to Android logcat tagged `DecenzaAutoRelaunch` so we can pull logs after a problem update.
4. If a user reports "the app didn't reopen after an update," we can ask them to share recent logs (or a debug-log dump if there's one) and see whether: (a) receiver didn't fire at all, (b) receiver fired but `startActivity()` threw, (c) receiver fired and `startActivity()` returned but Android dropped the launch silently.

The persisted timestamp survives process death and can be surfaced in Settings → About → Diagnostics for self-diagnosis.

**Alternatives considered:**

- *Telemetry to a server.* Rejected — no existing telemetry pipeline; not worth building one for a single experiment.
- *Logs only, no persisted state.* Rejected — logcat is not always retrievable from a user's tablet; a UI surface is more diagnosable.

### D4: Graceful degradation when permission is denied

If `Settings.canDrawOverlays(this)` is false at the time the receiver fires, the receiver still attempts `startActivity()` but does not crash on failure. The relaunch silently does not happen, and the user has the same experience as today. This means the receiver code path is uniform regardless of permission state — simpler to reason about and matches Android's "try, fail silently" model for BAL violations.

### D5: Settings storage location for the diagnostic flag

A single piece of state needs persisting: "was the most recent launch the result of an auto-relaunch attempt?" Per the project's settings-architecture rules, this goes in an appropriate domain sub-object (likely `SettingsApp`, since it relates to app lifecycle / update flow), not on the `Settings` façade directly. If `SettingsApp` does not yet have anything relevant, add the property there; do not introduce a new domain just for this.

### D6: Scope to Android only; no cross-platform abstraction

The code paths added by this change live in `android/src/...` (Java), `android/AndroidManifest.xml.in`, and `Q_OS_ANDROID`-gated sections of `src/core/updatechecker.{h,cpp}`. No abstraction layer is introduced for other platforms. Auto-relaunch is meaningless on iOS (no sideloaded self-update), Windows (installer relaunches via Inno Setup mechanism), or macOS/Linux (no in-app update mechanism today).

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| **It just doesn't work on Samsung One UI.** | **Significantly likely** given prior work (commits `0db30f00`, `208655c8`) confirmed BAL is aggressively enforced on this device. The whole point of the change is to find out whether SAW survives where the PendingIntent opt-in did not. D3 gives us the diagnostic signal. If it doesn't work, revert per the precedent in commit `5e19c785`. |
| **We re-run the same revert cycle from April.** Three weeks ago the conclusion was "the UX isn't actually better than the user tapping the app icon." If SAW works mechanically but is still no better than tapping, we end up reverting again. | Before implementing, decide: if SAW works, is "no setup, one tap" really worse than "one-time SAW grant + zero taps"? If unclear, defer this change. Don't repeat the cycle. |
| **Future Android version closes the SAW exemption.** Google narrowed the SAW + foreground-service exemption in Android 15. The activity-start exemption may follow. | D3 surfaces the regression on whichever update first fails to relaunch. We can revert or fall back to documentation at that point. |
| **User grants SAW once and forgets they granted it.** Permission persists indefinitely. | Surface the granted state visibly in Settings with a clear toggle to revoke (via deeplink to the system settings page). |
| **Samsung's OneUI prompts the user about a "permission needed by Decenza".** Some OEM ROMs show extra warnings when SAW is granted. | Acceptable — it's a one-time prompt and the user already chose to opt in. Document in release notes. |
| **The persisted `last_relaunch_was_auto` flag races with normal launches.** If a user manually opens the app after an update before the receiver could have fired, the flag would not be set and we might wrongly conclude "the receiver didn't fire" when it just hadn't yet. | The flag is set by the receiver path, not by `Activity.onCreate`. Manual launch leaves it false; receiver-launched leaves it true. Same Activity reads both correctly. |
| **`getLaunchIntentForPackage` returns null** for some odd reason (manifest misconfig, OEM weirdness). | Log a warning and skip the `startActivity()`. Same outcome as today. |
| **Permission gets revoked by Android's auto-revoke / hibernation** for unused permissions. | Re-check `canDrawOverlays()` on every relevant entry point; offer to re-grant via the same UX path. |
| **Two-stage receiver delivery on some ROMs**: the receiver fires before the new APK has fully replaced the old, causing `startActivity()` to launch a still-being-replaced activity. | Use `FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TOP` and rely on the system's activity manager to resolve correctly. If we see crashes on launch, add a brief retry. |
| **Tests cannot verify this works.** Qt Test doesn't run inside an Android update lifecycle. | Accept that test coverage is limited to: (a) Java unit-test the receiver's `Intent` building logic; (b) C++-side unit-test the auto-relaunch-detection logic given a known intent extra. The end-to-end behavior is verified manually by performing real updates on real tablets. |

## Migration Plan

1. Ship the change behind no flag (always-on in code), but with the permission un-granted by default. Users who don't engage with the new prompt have today's behavior unchanged.
2. Jeff dogfoods on his own Samsung dev tablet for one or two weekly update cycles.
3. If the auto-relaunch fires reliably for Jeff, surface the in-app prompt to all users.
4. After two further weekly release cycles, look at diagnostic state across the user base (via shot debug logs that already exist, if users share them on visualizer.coffee or in support threads).
5. If empirically broken, revert the manifest entries and Java receiver; keep the diagnostic logging as documentation that "we tried this; it didn't work on $version." This change is small and revertable in a single follow-up PR.

## Open Questions

- Where exactly should the in-app permission prompt live? Options: a one-time card on the Updates page when an update first downloads; an opt-in row in Settings → About / Settings → Updates; a dialog the first time a successful auto-relaunch *could have happened* (post-install) but didn't because permission wasn't granted. Decision can be deferred to the implementation phase.
- Should the diagnostic flag be exposed in the UI (Settings → About → "Last update auto-relaunched: yes/no/timestamp"), or only in logs? Probably exposed — it's the only way a non-technical user can self-report whether the feature is working for them.
- What's the right behavior if a future update changes the receiver's intent-extra key or the storage location of the diagnostic flag? Probably: don't change them. Treat them as a stable contract once shipped.
- Is the receiver delivered before or after `ApkInstaller`'s post-success `nativeOnInstallStatus` callback to C++ on Decenza's current Android versions? The C++ side already does some cleanup on `STATUS_SUCCESS`. Worth checking the actual ordering empirically — but a race is unlikely to matter because the receiver runs in a fresh process.
