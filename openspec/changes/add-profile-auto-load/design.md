# Design: Profile Auto-Load

## Why this shape

DSx2's auto-load mechanism is *simple on purpose* — one filename string, one state-change handler. The user-facing model is "pin a profile, it's the one we come home to." We mirror that simplicity:

- **One string of state** (`autoLoadProfileFilename`) plus one tunable (`autoLoadRevertMinutes`). No list, no per-time-of-day scheduling, no enable bool. Empty filename = feature off.
- **One entry point** (`loadAutoLoadProfileIfNeeded()`) called from three trigger sites. All policy (no-op when already active, clear-and-toast when stale, no-op when empty) lives in the entry point so trigger sites stay one-line.
- **One canonical UI location**. The strip on `ProfileSelectorPage` is the only place the revert minutes lives — same page where the auto-load profile is pinned, same page where it's visualised. No "settings tab" split.

## Trigger trio: why these three, why no more

| Trigger | Why include |
|---|---|
| App startup | Cold-launch must respect the pinned default. If the app was killed mid-experiment, the user's intent on next launch is the auto-load, not whatever was last active. |
| DE1 wake from sleep (`Sleep → Idle`) | Matches DSx2's only handler. Captures the "left the machine alone overnight on a one-off profile" case. The DE1's own sleep timeout is the implicit "amount of time" upper bound. |
| N minutes idle on the Idle page | DSx2 has nothing equivalent and we wanted it. Catches the case where the DE1 hasn't slept (high sleep timeout, or stay-awake) but the user has walked away mid-experiment. `0` disables this trigger so users who want pure DSx2 semantics can opt out. |

What's deliberately **not** a trigger:

- **Per-shot completion** — different feature (transient-profile flow). Bolting it on muddies the mental model.
- **Screensaver entry/exit** — wake-from-sleep already covers the post-screensaver case.
- **Phase changes other than Sleep→Idle** — would fire on Espresso→Idle (after every shot), which is the per-shot revert above by another name.

## The N-minutes timer — why it's not a CLAUDE.md violation

`CLAUDE.md` forbids timers as guards/workarounds. The N-minutes timer here is a **UI behavior timer** ("after N minutes of inactivity, do X"), exactly the family the rule explicitly allows. Precedents already in the codebase:

- `qml/main.qml:386` — `sleepCountdownTimer` decrements `sleepCountdownNormal` once per minute and triggers `triggerAutoSleep()` at zero. Same shape, same family.
- `qml/pages/PostShotReviewPage.qml:71` — auto-close after configured timeout.

We **reuse** the existing `sleepCountdownTimer`'s tick rather than adding a second `QTimer`. The auto-load countdown is a sibling integer on `root`, decremented in the same `onTriggered` handler. Zero new timers, zero new polling — just one more integer to decrement per minute.

## Why "Selected list" eligibility

The Selected list is the user's canonical "profiles I care about." Built-in profiles enter it through `selectedBuiltInProfiles`; user profiles enter it by virtue of not being in `hiddenProfiles`. If we let the user pin any profile in the system (including hidden ones, or built-ins they've explicitly de-selected), we'd be pinning a profile they've already told us they don't want to see — and the strip would happily render a profile title from a row that no longer appears in the selector. Tying eligibility to "currently in the Selected list" closes that loop:

- The menu item only appears on Selected-list rows.
- The fire-time check resolves the pinned filename against the Selected list and gracefully clears + toasts if it's no longer there.
- A user who hides their auto-load profile, intentionally or accidentally, gets an explicit message rather than silent surprise on the next wake.

## Stale-target handling

Five ways the pinned profile can become stale:

1. **Deleted entirely** (`ProfileManager.deleteProfile`) — file gone.
2. **Hidden** (user-side: `addHiddenProfile`) — file present but no longer in Selected.
3. **Built-in deselected** (`removeSelectedBuiltInProfile`) — file present but no longer in Selected.
4. **Filename changed** — today, filenames are derived from titles at create-time; renames don't move the file. So this is currently a non-case. Documented here so future filename-rename work knows the contract.
5. **Settings-bundle imported from another device that doesn't have the profile** — the imported `autoLoadProfileFilename` refers to a file that doesn't exist locally.

All five collapse to one check at the entry point: *if `autoLoadProfileFilename` doesn't resolve to a profile in the Selected list, clear it and toast.* No proactive scrubbing on delete/hide is needed (though we *could* clear it eagerly in `setHiddenProfiles` / `removeSelectedBuiltInProfile` / `deleteProfile` to keep the strip from briefly showing a profile that was just hidden — see Tasks §4 for that nice-to-have).

## DE1 previous-state tracking

`DE1Device` exposes `state` and emits `stateChanged()`, but does not expose `previousState`. Two options:

1. **Add `previousState` to `DE1Device`** — a real Q_PROPERTY, persists across QML listeners. Slightly more disruptive (touches a core class for one feature) but reusable.
2. **Track previous state in QML** — a local property on `root` in `qml/main.qml` that the auto-load `Connections` block updates after each `stateChanged()` call.

Going with option 2 for this change. Single consumer, lives next to the other state-change handlers in `main.qml`, no core API change. If a future change wants the same data, promote it to `DE1Device` at that point.

## Activity-reset hook for the idle countdown

The countdown needs to reset on any user interaction so leaving the Idle page open while reading specs doesn't silently swap your profile. Sources of "user activity":

- `MachineState.phaseChanged` — already resets `sleepCountdownNormal`. Covers explicit user actions that change phase (starting a shot, etc.).
- Touch/mouse input on the page — not currently hooked at the root level. The screensaver inactivity detection must hook this somewhere, since the screensaver triggers on no-input.

Tasks §3 finds and reuses the screensaver's existing inactivity-detection signal. If none exists at a usable layer, we add a thin top-level `Pointer Handler` in `main.qml` that emits an `userActivity` signal, and both the existing sleep countdown and the new auto-load countdown listen to it. (Cost: ~5 lines.)

## MCP surface — three tools, not one

Tempting to consolidate into a single `profiles_set_auto_load(filename?, revertMinutes?)` with empty `filename` meaning "clear." Rejected because:

- The advisor needs **clear semantic separation** between "set this profile as auto-load" and "disable auto-load." Overloading one tool with a magic-empty-string convention makes the tool description ambiguous and invites prompt drift.
- `profiles_get_auto_load` is a separate read with no write side, which is a clean access-level boundary (`read` vs `settings`).

Three tools, clear access boundaries, clear single-responsibility.

## Decisions left explicit (so reviewers don't re-litigate them)

- **Single auto-load profile globally.** No multi-pin, no per-context auto-load.
- **`autoLoadRevertMinutes` default = 5.** Mirrors what the user asked for; tunable per-user.
- **`autoLoadRevertMinutes = 0` disables the inactivity trigger only.** Startup and wake-from-sleep still fire. There is no single "disable feature" toggle — clearing the filename is the disable.
- **Strip is the only home for `autoLoadRevertMinutes`.** No settings-tab entry. Settings-search index gets a single hit that lands users on the page.
- **Built-in profiles eligible.** Same Selected-list gate as user profiles.
- **Pin icon, not "AUTO" pill, not row stripe.** Matches the existing sparkle pattern; language-independent.
- **Menu item is contextual** ("Set" / "Disable" on the same MenuItem), not two separate items. Keeps the menu short on every other row.
- **No "Are you sure?" prompts.** Set/clear is a one-tap action; the strip and the row icon make state obvious; toast confirms.
