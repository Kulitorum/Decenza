# Change: Auto-load a chosen profile on app start, DE1 wake, and prolonged idle

## Why

A common DSx2 workflow is: pick a "default" profile (the one you want to live on most days), occasionally swap to an experimental one for a single shot, and have the machine quietly return you to the default. DSx2 implements this with a single favorite-slot marker plus a `Sleep → Idle` state-change handler that reloads the marked profile on every DE1 wake. The setup is one long-press on a favorite slot's auto-load label; from that point on, the machine "comes home" to that profile whenever it wakes.

Decenza has no equivalent. Today, switching from an experimental profile back to your default is a manual three-tap walk: ProfileSelector → search → tap. Worse, if the machine sleeps overnight on a one-off profile, you wake it tomorrow morning to that same one-off profile — no auto-revert at all.

This change adds the DSx2-style "pin one profile as auto-load" behavior, with three concrete triggers (app startup, DE1 wake-from-sleep, and N-minutes-idle on the Idle page) and a small surface area: one menu item in the ProfileSelector overflow, one pin icon next to the title of the chosen profile, one optional status strip at the top of the page when an auto-load is set, and three MCP tools so the AI advisor (and any remote MCP client) can set/clear/inspect the auto-load.

## What Changes

### Settings

- **NEW** `Settings.app.autoLoadProfileFilename` (`QString`, default `""`) — filename of the pinned profile, or empty when no auto-load is configured. Single-valued: setting a new filename replaces the previous one. Cleared automatically if the referenced profile is deleted or moved out of the Selected list at fire-time.
- **NEW** `Settings.app.autoLoadRevertMinutes` (`int`, default `5`) — minutes of inactivity on the Idle page before the auto-load profile is restored. `0` disables the inactivity trigger without affecting startup or wake-from-sleep triggers.
- Both properties sit in the existing `// Profile management` block in `src/core/settings_app.h`, alongside `favoriteProfiles`, `hiddenProfiles`, and `currentProfile`. Both round-trip through `SettingsSerializer` so backups capture them.

### Triggers

A new tiny controller (or a method on `ProfileManager`) exposes a single entry point `loadAutoLoadProfileIfNeeded()` that:

1. Returns immediately if `autoLoadProfileFilename` is empty.
2. Clears the setting + emits a toast `"Auto-load profile is no longer available"` if the filename does not resolve to a profile currently in the Selected list.
3. Returns without loading if the auto-load filename is already the active profile (no-op when already loaded).
4. Otherwise calls `ProfileManager::loadProfile(filename)`.

The entry point is invoked from three places, all in `qml/main.qml`:

- **Trigger 1 — App startup**: fired once after `ProfileManager` and the Settings facade are initialised (alongside the existing post-init wiring near the top of `Component.onCompleted`).
- **Trigger 2 — DE1 wake from sleep**: a `Connections { target: DE1Device }` watches `onStateChanged` and fires the entry point when the previous state was `Sleep` and the current state is `Idle`. Previous-state tracking is added as a local QML property since `DE1Device` does not expose it today.
- **Trigger 3 — N-minutes idle on Idle page**: a new countdown property `autoLoadIdleCountdown` ticks on the existing 1-minute `sleepCountdownTimer`'s `onTriggered` (no second timer needed). The countdown is reset to `Settings.app.autoLoadRevertMinutes` whenever the current page changes, any user input is registered (touch/mouse — wired off the same activity hook the existing screensaver inactivity uses), or `operationActive` becomes true. When the countdown hits zero on the Idle page (`pageStack.currentItem.objectName === "idlePage"`), the entry point fires. When `autoLoadRevertMinutes` is `0`, this trigger is disabled entirely and the countdown does not run.

### UI: ProfileSelectorPage

- **NEW** Menu item in the existing overflow Menu (`qml/pages/ProfileSelectorPage.qml` line ~432) labelled `"Set Auto-Load"` when the row is not the current auto-load, and `"Disable Auto-Load"` when it is. Single MenuItem whose `text` and `onTriggered` switch on the `isAutoLoad` row property. The item is visible only for rows in the Selected list (all profile types: D-Flow, A-Flow, Pressure, Flow; built-in or user). It sits between "Copy Profile" and the destructive-actions separator.
- **NEW** A `qrc:/icons/pin.svg` image placed beside the title in the same layout slot as the existing sparkle icon (line ~296), visible only when `modelData.name === Settings.app.autoLoadProfileFilename`. Colored `Theme.primaryColor` via `MultiEffect`, sized 14×14, with `Accessible.name: "Auto-load profile"`.
- **NEW** A status strip at the top of the page (above `viewFilter`), visible only when `Settings.app.autoLoadProfileFilename` is non-empty AND resolves to a Selected profile. Shows: a pin icon, the resolved profile title, the revert-minutes `ValueInput` (1..60, step 1, "0" rendered as "Never"), and an inline `[×]` button that calls `Settings.app.setAutoLoadProfileFilename("")`. The strip is the only place the user can edit `autoLoadRevertMinutes`; it does not appear in any settings tab.

### MCP

Three new tools registered in `src/mcp/mcptools_profiles.cpp` (reads) and `src/mcp/mcptools_write.cpp` (writes), following existing field-naming conventions (units in names, no Unix timestamps, human-readable values):

- **NEW** `profiles_get_auto_load` (read): no arguments. Returns `{ filename, title, revertMinutes }` when an auto-load is set; returns `{ filename: "", revertMinutes }` when none is set. `revertMinutes` is always included so callers can inspect the configured timeout regardless of whether an auto-load is currently pinned.
- **NEW** `profiles_set_auto_load` (settings): args `{ filename: string, revertMinutes?: int }`. Validates that the filename exists and is in the Selected list. If `revertMinutes` is supplied, updates that too. Returns `{ success, filename, title, revertMinutes }` or an `error` field on failure. Replaces any prior auto-load.
- **NEW** `profiles_clear_auto_load` (settings): no required args. Sets `autoLoadProfileFilename` to empty. Returns `{ success: true }`. Does not modify `revertMinutes` (the timeout setting is preserved across enable/disable cycles).

### Asset

- **NEW** `resources/icons/pin.svg` — line-art pin icon matching the existing icon set's stroke style. Sourced from Feather Icons "map-pin" (MIT-licensed) or hand-authored equivalent. Registered in `CMakeLists.txt` under the resource bundle alongside the existing icon list.

### Translations

- **NEW** `profileselector.menu.set_auto_load` → "Set Auto-Load"
- **NEW** `profileselector.menu.disable_auto_load` → "Disable Auto-Load"
- **NEW** `profileselector.accessible.auto_load_profile` → "Auto-load profile"
- **NEW** `profileselector.accessible.set_auto_load` → "Set as auto-load profile"
- **NEW** `profileselector.accessible.disable_auto_load` → "Disable auto-load"
- **NEW** `profileselector.strip.auto_load_label` → "Auto-load:"
- **NEW** `profileselector.strip.revert_after` → "revert after"
- **NEW** `profileselector.strip.minutes_short` → "min"
- **NEW** `profileselector.strip.never` → "Never"
- **NEW** `profileselector.strip.clear_aria` → "Disable auto-load"
- **NEW** `profileselector.toast.auto_load_set` → "Auto-load set to {0}"
- **NEW** `profileselector.toast.auto_load_disabled` → "Auto-load disabled"
- **NEW** `profileselector.toast.auto_load_stale` → "Auto-load profile is no longer available"

### NOT in scope

- **"Revert to auto-load after every shot"**. A useful adjacent feature, but a separate behavior with different semantics (transient-profile flow vs. default-profile flow). Track separately if requested; do not bundle.
- **Multiple auto-load profiles bound to time-of-day / day-of-week** (e.g., decaf at night). Adds a list, a scheduler, and a UI layer for no proven demand. Single-value is the DSx2 model and the simplest mental model.
- **`ScreensaverPage` as a trigger surface for the N-minutes-idle trigger**. The wake-from-sleep trigger already covers the post-screensaver case; double-counting adds no value.
- **A Settings-tab home for `autoLoadRevertMinutes`**. The strip is the only place to tune it. If real-world feedback later asks for a settings-search-discoverable entry, add a one-line breadcrumb under `SettingsScreensaverTab` then.
- **Auto-load while built-in profile is hidden but selected via favorites**. Eligibility is "currently in the Selected list" — the canonical, in-UI definition of "a profile the user has chosen to keep." If a user hides their auto-load profile, the next firing clears it gracefully.

## Impact

- Affected specs: `profile-auto-load` (NEW capability), `mcp-server` (MCP surface gains three tools — out-of-band addition to the existing capability spec).
- Affected code:
  - `src/core/settings_app.{h,cpp}` — two new properties + getters/setters/signals.
  - `src/core/settingsserializer.cpp` — include both keys in profile-bundle export/import.
  - `src/profiles/profilemanager.{h,cpp}` — `loadAutoLoadProfileIfNeeded()` entry point and selected-list eligibility helper.
  - `qml/main.qml` — startup invocation, Sleep→Idle wiring, idle-countdown integration.
  - `qml/pages/ProfileSelectorPage.qml` — overflow MenuItem, row pin icon, top status strip, `isAutoLoad` row property.
  - `qml/components/SettingsSearchIndex.js` — index a single search hit for "auto-load" that lands the user on `ProfileSelectorPage` (cross-tab discoverability).
  - `src/mcp/mcptools_profiles.cpp` — `profiles_get_auto_load`.
  - `src/mcp/mcptools_write.cpp` — `profiles_set_auto_load`, `profiles_clear_auto_load`.
  - `resources/icons/pin.svg` — new asset.
  - `CMakeLists.txt` — register `pin.svg`.
  - `tests/tst_profilemanager.cpp` (or new file) — entry-point behavior tests.
  - `tests/tst_settings.cpp` — round-trip tests for the two new keys.
- Behavior change: users who set an auto-load profile will see Decenza load it on every cold start, on every DE1 wake from sleep, and after `autoLoadRevertMinutes` of inactivity on the Idle page. Users who never set one see no change. Settings backups roundtrip the auto-load filename and revert minutes.
- Performance: zero new timers (reuses the existing 1-minute `sleepCountdownTimer` tick). Profile load is the existing `ProfileManager::loadProfile` path with a no-op guard for "already active," so a re-fire while the auto-load profile is loaded costs one filename comparison.
