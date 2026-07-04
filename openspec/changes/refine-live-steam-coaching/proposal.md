## Why

PR #1412 adds a during-steam coaching banner (`LiveSteamCoach`) whose headline value — a spoken "stop" so the user can keep their eyes on the pitcher — does not actually work in the default configuration, and whose stop cue is driven by a time *prediction* rather than the real steam-end event. A multi-agent review surfaced two blocking silent-failure bugs plus several correctness and discoverability gaps. This change fixes those issues and re-shapes the feature so it is **optional, off by default, and cleanly separates the visual banner from the spoken audio cues** (two independent opt-ins), rather than quietly depending on unrelated accessibility prefs.

## What Changes

- **BREAKING (default behavior):** the live steam coaching feature ships **off by default**. Both the visual banner and the spoken cues are opt-in.
- **Split one toggle into two independent settings:** a *visual* steam-coaching toggle (on-screen banner) and a separate *audio* steam-coaching toggle (spoken cues). Neither implies the other; each is off by default.
- **Make speech a service, decoupled from the UI and the accessibility master switch.** Today `AccessibilityManager::announce()` no-ops unless `accessibility/enabled` (default false) is on, so the spoken "stop" is silently dead for most users — and the speak call lives inside the visual banner QML, coupling audio-only mode to a hidden UI element. The coach itself must request speech whenever the dedicated *audio steam-coaching* toggle is on, routed past the screen-reader/master a11y switch.
- **Drive the end-of-steam cue off the actual steam-end event, not a predicted `[timeout-1s, timeout]` window** — and phrase it as a completion notification ("Steam done"), silent on an early manual abort. The machine knows the exact moment steam flow ends (the substate→Puffing/Ending transition that stops the shot timer); firing from that event guarantees the cue can never be missed when the local clock and firmware countdown drift (aligns with the project's no-timers/heuristics-as-guards principle). The predicted-time path survives only as the anticipatory "almost" heads-up.
- **Remove the spoken-spacing governor** that silently swallowed the short-steam "almost" heads-up. Every cue is already one-shot latched (at most four per operation), so latching is the only rate control needed.
- **Move the toggles onto the steam configuration page and correct the descriptions.** Once audio is decoupled from accessibility this is a steam UX feature; the two toggles live only in the steam configuration section on `SteamPage` (with Duration / Steam Flow / Temperature), with the voice description stating it speaks independently of the accessibility settings (removing the misleading "gated by extractionAnnouncements" wording — a pref nothing else actually consumes).
- **Add unit tests** (`tst_livesteamcoach`) covering untimed steam, short steam, milestone ordering, one-shot latches, state reset, and the guaranteed spoken-stop contract.
- **Cleanup:** remove the stale `LiveShotCoach`/"espresso page" reference in `LiveCoachingBanner.qml` (that companion PR was closed), and add debug breadcrumbs on the silent degradation paths (null machine state, unset steam timeout).

## Capabilities

### New Capabilities
- `live-steam-coaching`: during-steam milestone cues (stretch → roll → almost → stop) with an on-screen banner and optional spoken audio. Defines the feature gating (off by default; independent visual and audio opt-ins), the audio path's independence from the accessibility master switch, the event-based stop-cue guarantee, and milestone pacing/ordering rules.

### Modified Capabilities
<!-- No existing spec's requirements change; audio routing independence is captured inside the new live-steam-coaching capability rather than by amending accessibility-announcements. -->

## Impact

- **Code:** `src/ai/livesteamcoach.{h,cpp}` (speech-as-a-service `speakRequested` signal, event-driven completion cue, governor removal, disabled-idle, logging), `src/core/settings_app.{h,cpp}` (split into visual + audio settings, defaults false), `src/core/accessibilitymanager.{h,cpp}` (guard-free `announceCoaching` entry point), `src/machine/machinestate.{h,cpp}` (new `steamFlowStopped()` signal), `src/controllers/maincontroller.{h,cpp}` (speak wiring), `qml/components/LiveCoachingBanner.qml` (visual-only; stale comment), `qml/pages/SteamPage.qml` (mount wiring + the two toggles in the steam configuration section), `qml/pages/settings/SettingsLanguageTab.qml` (old toggle removed).
- **New tests:** `tests/tst_livesteamcoach.cpp` registered via `add_decenza_test`.
- **Settings migration:** the previously-added `steam/liveSteamCoachingEnabled` key (default true) is replaced by two keys defaulting false; since PR #1412 is unmerged no released install carries the old key, so no runtime migration is required.
- **i18n:** new/updated `TranslationManager` keys for the two settings labels/descriptions.
- **No BLE protocol, profile, or DB schema changes.**
