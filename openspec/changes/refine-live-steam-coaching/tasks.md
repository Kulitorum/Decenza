## 1. Settings: split into two default-off toggles

- [x] 1.1 In `SettingsApp` replace `liveSteamCoachingEnabled` with two properties: `steamCoachVisualEnabled` and `steamCoachAudioEnabled`, each with getter/setter/NOTIFY, defaulting to `false` (QSettings keys `steam/steamCoachVisualEnabled`, `steam/steamCoachAudioEnabled`).
- [x] 1.2 Remove the old `steam/liveSteamCoachingEnabled` property/key entirely (no migration needed — unmerged feature).
- [x] 1.3 Remove the coaching toggle block from `SettingsLanguageTab.qml`; add both toggles as a visually distinct group at the bottom of the steam configuration section of `SteamPage.qml` (below Milk pitcher: separator + small "Coaching" group header, then two rows following the section's row pattern) — "Coaching Banner" and "Coaching Voice" rows with `StyledSwitch`es bound to the two global `Settings.app` prefs. The rows above are per-pitcher-preset while these are global, so the group must read as separate — not as two more preset rows. The voice description must state it speaks independently of the accessibility settings. Add `TranslationManager` keys for labels/descriptions; ensure switches carry `accessibleName`.

## 2. Coach: speech as a service (page-independent)

- [x] 2.1 In `LiveSteamCoach`, read `steamCoachAudioEnabled` (via `m_settings`) and emit a new `speakRequested(QString text, bool interrupt)` signal when a spoken cue fires; remove the `cueSpeak` Q_PROPERTY from the cue surface.
- [x] 2.2 Cache the two toggle values (refreshed via their NOTIFY signals) and early-return from per-tick evaluation when both are off (spec: disabled coach does no work).
- [x] 2.3 In `MainController`, connect `LiveSteamCoach::speakRequested` to the announcement layer (task 3.1) once at construction.

## 3. Audio routing independent of the accessibility master switch

- [x] 3.1 Add `AccessibilityManager::announceCoaching(text, interrupt)` that calls `routeAnnouncement()` directly — reusing TTS/platform routing and the existing drop-logging — without the `m_enabled` master-switch guard.
- [x] 3.2 Verify the routing layer logs the reason whenever speech cannot be produced (no silent discard); add a breadcrumb if any gap remains.

## 4. Event-based, exactly-once completion cue

- [x] 4.1 Add a `steamFlowStopped()` signal to `MachineState`, emitted at the existing steam flow-stop site (`stopShotTimer()` under `!isFlowing() && state==Steam`, machinestate.cpp ~554-561). Pure addition — no change to existing consumers.
- [x] 4.2 In `LiveSteamCoach`, connect to `steamFlowStopped()`: if not already fired and the steam is timed with `remaining <= ALMOST_REMAINING_SEC`, emit the completion cue (notification wording, e.g. "Steam done"; new i18n key); if flow stopped earlier than that (manual abort) or the steam is untimed, stay silent.
- [x] 4.3 Remove the predicted `[timeout−1s, timeout]` stop window (`STOP_REMAINING_SEC`, `m_firedStop` prediction path); keep the predicted-time logic only for the anticipatory "almost" cue. Rename the latch to match the completion semantics.

## 5. Remove the spoken-spacing governor

- [x] 5.1 Delete `MIN_SPOKEN_SPACING_SEC`, `m_lastSpokenClock`, and the spacing gate in `emitCue`; one-shot latches are the only rate control. Update header comments accordingly.

## 6. Banner: visual-only

- [x] 6.1 In `LiveCoachingBanner.qml`, remove the speak logic and the `AccessibilityManager.extractionAnnouncementsEnabled` dependency; the banner renders `cueText`/`cueSeverity`/`cueActive` gated on `coachEnabled` only.
- [x] 6.2 Remove the stale comment referencing "LiveShotCoach on the espresso page"; describe only the steam mount (companion espresso PR was closed).
- [x] 6.3 In `SteamPage.qml`, wire `coachEnabled: Settings.app.steamCoachVisualEnabled`.

## 7. Silent-degradation breadcrumbs

- [x] 7.1 Add debug-log breadcrumbs when the coach is inert due to a null `machineState`, or degrades to stretch-only because `brew()` is null / `steamTimeout <= 0`.

## 8. Tests

- [x] 8.1 Add the needed friend/test hooks (`DECENZA_TESTING` pattern); create `tests/tst_livesteamcoach.cpp` and register it via `add_decenza_test`.
- [x] 8.2 Cover gating: both prefs off → no cues and no evaluation work; visual-only → cues on the surface, no `speakRequested`; audio-only → `speakRequested` fires, cue surface still updates for state; toggling a pref between two steam operations takes effect on the second without any re-construction.
- [x] 8.3 Cover pacing: untimed steam (stretch only, no completion), short steam (roll skipped; almost AND completion still delivered), milestone ordering, one-shot latches, state reset across two consecutive steam operations.
- [x] 8.4 Cover completion semantics: `steamFlowStopped()` while elapsed is below the old prediction window → completion cue delivered exactly once with `speakRequested` when audio on; `steamFlowStopped()` well before target (manual abort) → no completion cue; second `steamFlowStopped()` → no duplicate.
- [x] 8.5 Assert no WARN/stderr output on any covered path (per TESTING.md).

## 9. Smoke-feedback refinements (added during 10.2 on-device testing)

- [x] 9.1 Mount the banner in the steaming view's layout (warning-banner slot, between preset pills and countdown) instead of a floating overlay that overlapped the pills; remove the overlay mount.
- [x] 9.2 Cues persist until replaced or steam ends — remove the 5s auto-dismiss (timer, dismissed state, Connections) from `LiveCoachingBanner.qml`.
- [x] 9.3 Gate ALL cues on a milk-derived duration: `durationMilkDerived` Q_PROPERTY on `LiveSteamCoach`, bound from `SteamPage.steamTimeoutScaled`; without it show one visual-only pill ("No coaching — milk weight not captured") and stay otherwise silent (including "done"). Defer the opening cue to the first tick to avoid the page-handler ordering race.
- [x] 9.4 Update the Coaching group description to state the milk-weight requirement; tests for the gate (`notMilkDerived_showsPillOnly_nothingSpoken`) and first-tick cue start; spec updated (new requirement + persistence).
- [x] 9.5 Fix `tst_livesteamcoach` polluting the developer's real QSettings store (snapshot/restore in init/cleanup — the polluted store was what made the toggle show ON during smoke testing).

## 10. Verify

- [x] 10.1 Build app + tests (via Qt Creator) and run `tst_livesteamcoach` green.
- [ ] 10.2 On-device smoke: both toggles off by default (no banner, no voice); with weight-timed steaming + captured milk: voice-only (a11y master switch off) → spoken cues incl. "Steam done" at actual steam end; banner-only → silent banner, cues persist until replaced, no overlap with pills; without captured milk → only the "No coaching" pill, nothing spoken; early manual stop → no completion announcement.
- [x] 10.3 Update the PR #1412 description to reflect the split settings, off-by-default, service-level audio, event-based completion cue, and the milk-derived coaching gate.
