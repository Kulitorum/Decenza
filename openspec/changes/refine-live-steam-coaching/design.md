## Context

PR #1412 introduced `LiveSteamCoach`, a local milestone coach for steaming (stretch → roll → almost → stop) rendered by `LiveCoachingBanner.qml` and optionally spoken via `AccessibilityManager`. A four-agent review found the engineering solid but the *feature behavior* flawed:

- The spoken "stop" — the feature's stated headline — routes through `AccessibilityManager::announce()`, which returns early unless the master `accessibility/enabled` pref is on (default **false**). The banner's speak gate only checks `extractionAnnouncementsEnabled`, so on a default install the spoken cue is silently dead.
- The stop cue fires only if a `shotTimeChanged` tick lands in the predicted window `[steamTimeout − 1s, steamTimeout]`, but the shot timer freezes the instant steam flow ends (`MachineState::stopShotTimer()` on the steam substate→Puffing/Ending transition). Clock drift can skip the window entirely, so the safety cue can silently never fire.
- The spoken-spacing gate uses the opening stretch cue's clock as its baseline, silently swallowing the "almost" heads-up on short steams.
- One combined `steam/liveSteamCoachingEnabled` setting (default true) conflates the visual banner and the audio cues.

A follow-up spec review added:
- **Speech is mounted in the wrong layer.** The speak call lives in `LiveCoachingBanner.qml`'s `Connections` handler, coupled to the visual component. (Steam is page-bound — leaving `SteamPage` stops steam — so the page is always present while steaming; the problem is not page lifetime.) In audio-only mode the speech would depend on a hidden visual component's handler, and the speak decision is untestable from C++. Audio is a service concern, not a UI concern.
- **The stop wording is stale once event-based.** An imperative "Stop — steam finishing" written for a 1s-early prediction becomes wrong when fired *at* the flow-stop event; and announcing anything after a deliberate early manual stop is noise.
- **No double-announce risk ever existed**: nothing in the codebase consumes `extractionAnnouncementsEnabled` (only settings/backup/MCP plumbing). The PR's rationale for reusing that pref was hollow; the new audio toggle will be the only steam-speech path.
- The toggles were placed in the accessibility section of `SettingsLanguageTab`; once audio is decoupled from accessibility this is a steam UX feature and belongs with the steam settings.

The user's directive: fix every issue, make the feature optional and **off by default**, and keep **audio independent from visual** (two separate opt-ins).

Constraints: CLAUDE.md — settings live in a domain sub-object; no timers/heuristics as guards (prefer events); all user-visible strings via `TranslationManager`; new C++ services get Qt Test coverage with no WARN output.

## Goals / Non-Goals

**Goals:**
- Two independent, default-off settings: visual banner and spoken audio.
- Speech produced by the coaching service itself — independent of the visual banner and of the a11y master switch.
- An event-based, guaranteed-once completion cue with correct notification semantics (silent on early manual abort).
- Every applicable milestone delivered on short steams; no spacing governor.
- Zero per-tick work when the feature is fully off.
- Unit tests locking in all of the above, including the spoken-completion contract.
- Remove the stale `LiveShotCoach` comment; add breadcrumbs on silent-degradation paths.

**Non-Goals:**
- No change to the time-fraction pacing of stretch/roll/almost (open-loop pacing is inherent — the DE1 does not measure milk temperature; that limitation is out of scope here).
- No espresso/"during-shot" coach (companion PR #1411 was closed).
- No new TTS backend; reuse the existing announcement routing, just gated differently.

## Decisions

### 1. Split the setting into `steamCoachVisualEnabled` and `steamCoachAudioEnabled`, both default false
Add two properties to `SettingsApp` (replacing the single `liveSteamCoachingEnabled`). Keys default false.
- *Alternative considered:* keep one setting with a sub-mode. Rejected — the user explicitly wants audio and visual decoupled, and two booleans are the simplest faithful model. Since #1412 is unmerged, no released install carries the old key, so no migration shim is needed.
- *Domain note:* keep both in `SettingsApp` (UI toggles, consistent with `screenCaptureEnabled`); QSettings keys under the `steam/` prefix.

### 2. Speech moves into the service: coach emits `speakRequested`, MainController wires it
`LiveSteamCoach` reads the audio setting itself and, when a cue warrants speech, emits `speakRequested(QString text, bool interrupt)`. `MainController` connects that signal once to the announcement layer. The banner becomes purely visual (`cueSpeak` is removed from the QML surface, and the banner's dependency on `extractionAnnouncementsEnabled` is dropped — nothing else consumes that pref for steam).
- *Why:* audio-only mode must not hinge on a hidden visual component's `Connections` handler staying wired, and the speak decision — the exact contract that silently failed in review (C1) — becomes unit-testable via `QSignalSpy` with no QML or AccessibilityManager involved. (Note: steam is page-bound — leaving `SteamPage` stops steam — so page lifetime is *not* a motivation; the page is always present while steaming.)
- *Alternative considered:* keep speaking in the banner's `Connections`, gated on the new audio setting. Functional (the banner stays instantiated on the always-present steam page even when hidden), and a smaller diff — but it couples audio to an invisible UI element and leaves the spoken-completion guarantee untestable. Rejected on those two grounds.

### 3. Route spoken cues past the a11y master switch via a dedicated entry point
Add `AccessibilityManager::announceCoaching(text, interrupt)` that calls `routeAnnouncement()` directly — reusing the TTS/platform routing and existing drop-logging — without the `m_enabled` master-switch guard. `MainController` connects `speakRequested` to it.
- *Alternative considered:* have the setting toggle flip `accessibility/enabled`. Rejected — hijacks an unrelated global (screen-reader ticks etc.). The audio path must be self-contained.
- If speech cannot be produced, the routing layer's drop-logging records why (spec: no spoken cue dropped without a diagnostic).

### 4. Event-based completion cue via a `MachineState::steamFlowStopped()` signal, with abort suppression
`MachineState` already detects the exact steam-end moment where it calls `stopShotTimer()` under `!isFlowing() && state==Steam` (machinestate.cpp ~554-561). Emit `steamFlowStopped()` there — a pure addition. `LiveSteamCoach` connects to it and, if `!m_firedCompletion`, decides:
- **Natural completion** (timed steam, flow stopped within the final anticipation window — `remaining ≤ ALMOST_REMAINING_SEC` — which also covers firmware/local clock drift): emit the completion cue, phrased as a notification ("Steam done"), spoken when audio is on.
- **Early manual abort** (flow stopped with more than the window remaining, or an untimed steam): stay silent — the user acted deliberately and needs no announcement.
The predicted-time path survives only as the *anticipatory* "almost" heads-up; the `[timeout−1s, timeout]` stop window is removed. The one-shot latch plus `emitCue` dedupe guarantee exactly-once; the frozen post-stop clock cannot re-trigger.
- *Alternative considered:* connect the coach to `onDE1SubStateChanged` directly. Rejected — duplicates flow-stop logic MachineState already owns; one semantic signal keeps the coach machine-agnostic.
- *Alternative considered:* widen the prediction window. Rejected — still a heuristic; can't guarantee delivery.

### 5. Delete the spoken-spacing governor entirely
Every cue is one-shot latched — at most four per operation, at most three spoken on a normal steam. The spacing governor protects against nothing and caused the swallowed-"almost" bug. Remove `MIN_SPOKEN_SPACING_SEC`, `m_lastSpokenClock`, and the gate; latching is the only rate control.
- *Alternative considered:* exempt distinct milestones from spacing. Rejected — carving exemptions from a mechanism with no remaining purpose is complexity without benefit.

### 6. Idle when fully disabled
When both settings are off, the coach skips all per-tick evaluation (early return keyed on the two settings' cached values, refreshed by their NOTIFY signals — event-based, no polling). Cheap, but principled: a disabled feature should cost nothing at 10 Hz.

### 7. Settings placement: the steam configuration section on `SteamPage` only
Both toggles live as the **last two rows** of the steam configuration area on `SteamPage` — at the bottom, below Milk pitcher — following that section's existing row pattern (label + secondary description on the left, control on the right): "Coaching banner" and "Coaching voice", the latter's description stating it speaks independently of the accessibility settings. Bottom placement keeps the frequently-adjusted brew parameters (Duration / Steam Flow / Temperature / Milk pitcher) in their current positions and puts the set-and-forget toggles after them. They appear nowhere else: the toggle PR #1412 added to `SettingsLanguageTab`'s accessibility section is removed, and nothing goes in `SettingsMachineTab`.
- *Why:* steam behavior is configured on the steam page, and that's where a user deciding about steam cues already is.
- *Scope boundary — make it visible:* every row above (Duration / Steam Flow / Temperature / Milk pitcher) is **per-pitcher-preset**; the coaching toggles bind to the global `Settings.app` properties and do not change with the selected preset. The UI must communicate this: render the two toggle rows as a visually distinct group at the bottom (separator + small "Coaching" group header), so switching presets visibly changes the rows above while the coaching group stays put. Do not style them as just two more preset rows.
- *Alternatives considered:* `SettingsLanguageTab` accessibility section (as PR #1412 had) — rejected, the a11y coupling is being removed; `SettingsMachineTab` steam section — rejected in favor of the steam page per user direction (one home, closest to use).

### 8a. Coaching requires a milk-derived duration (from on-device smoke feedback)
All cues — including "almost" and "done" — are gated on the session's duration being derived from the actual milk weight. Rationale: with a fixed 60 s preset and 200 mL of milk, the milk is destroyed long before the timer ends; a coach narrating that timer ("Almost there" at 48 s) actively endorses ruining it. Mechanism: `LiveSteamCoach` gains a settable `durationMilkDerived` Q_PROPERTY; `SteamPage` binds it to its existing `steamTimeoutScaled` state (true only while the current `steamTimeout` was weight-scaled this session; cleared at session end, pitcher change, and by manual ±5 s overrides). When coaching is enabled but the duration is not milk-derived, one informational pill — "No coaching — milk weight not captured", visual only, never spoken — explains the silence and points at the remedy.
- *Ordering note:* the page sets `steamTimeoutScaled` in its own `phaseChanged` handler, which runs after the coach's C++ slot, so the coach no longer fires the opening cue at phase entry — the first shot-time tick (~100 ms) drives it, by which time the page state is settled. Event-ordering, not a timer.
- *Alternatives considered:* keeping "almost/done" without the gate (rejected — they are the harmful part, not the harmless part); pacing off a remembered last-milk per pitcher (rejected — last time's milk isn't this time's milk, same trap in disguise).

### 8b. Banner placement and persistence (from on-device smoke feedback)
The banner mounts in the steaming view's `ColumnLayout` — the same slot as the existing live warning banner, between the preset pills and the countdown — instead of a floating overlay with a hardcoded top margin (which overlapped the pills). Cues persist until the next cue replaces them or the steam ends; the 5 s auto-dismiss timer was removed entirely (cues were easy to miss), making the banner fully declarative (no Timer, no Connections).

### 8. Tests: `tests/tst_livesteamcoach.cpp`
Drive the coach by setting `MachineState::m_phase`/`m_shotTime` (friend access per `DECENZA_TESTING` pattern) and emitting `phaseChanged`/`shotTimeChanged`/`steamFlowStopped`, plus `SettingsBrew::setSteamTimeout` and the two new toggles. Assert on the cue surface (`cueChanged` + properties) and on `speakRequested` via `QSignalSpy` — the speak path needs no AccessibilityManager. Cover: off-by-default (no cues, no work), visual-only vs audio-only, toggling between two operations, untimed steam, short steam, ordering, latches, reset across two operations, natural completion via event while the clock lags, early-abort silence, exactly-once completion. No WARN output on any path.

## Risks / Trade-offs

- **[New `MachineState` signal touches a hot, well-tested path]** → the emit sits exactly where `stopShotTimer()` is already called for steam; pure addition, covered by a new test.
- **[Abort-vs-completion threshold (`remaining ≤ ALMOST_REMAINING_SEC`) misclassifies an unusual stop]** → worst case is one spurious/missing "Steam done" notification — informational, not safety-critical; threshold is a named constant, tunable.
- **[Bypassing the a11y master switch could surprise a user who expects that switch to govern all speech]** → the voice toggle is its own explicitly-labeled opt-in, off by default, described as independent; screen-reader users are unaffected unless they opt in.
- **[Open-loop pacing remains]** → out of scope by decision; corrected copy avoids over-claiming, and the feature is opt-in.

## Migration Plan

No runtime data migration: PR #1412 is unmerged, so no released build persists `steam/liveSteamCoachingEnabled`. The change lands on the `pr-1412` branch (or a successor), replacing the single setting with two default-off settings before the feature ever ships. Rollback = revert the change; the feature returns to its prior (flawed) state or is dropped with the branch.

## Open Questions

- Exact completion-cue wording ("Steam done" vs "Steam finished") — settle with the i18n pass.
