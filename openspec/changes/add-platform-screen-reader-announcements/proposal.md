# Change: Route accessibility announcements through the platform screen reader

## Why

`AccessibilityManager::announce()` currently speaks every announcement through `QTextToSpeech` directly. On Android/iOS — where TalkBack/VoiceOver are active — our TTS engine talks **in parallel with** the OS screen reader, overlapping its utterances and confusing the user. When TalkBack is off, our TTS still speaks (gated only by our `ttsEnabled` setting), which is the opposite of what an a11y-aware app should do.

Qt 6.8+ exposes `QAccessibleAnnouncementEvent` (QML `Accessible.announce()`), which routes through the OS accessibility framework with Polite/Assertive politeness so periodic extraction announcements don't interrupt a swipe-read. We're already on Qt 6.10.3, so the API is available — we just don't use it.

This change makes the platform screen reader the primary announcement channel. `QTextToSpeech` stays available as an explicit user-selectable mode for users who want speech without enabling TalkBack/VoiceOver.

## Scope notes

This change was originally bundled with a high-contrast theme adaptation (Qt 6.10's `QStyleHints::accessibility()->contrastPreference`). The high-contrast work has been deferred — there's no current evidence of demand, the visual surface is large, and "what does Decenza look like in high contrast" needs design input that isn't engineering scope. The design notes for that work are preserved in a tracking issue for future pickup.

## What Changes

- **ADD** event-based announcement delivery on `AccessibilityManager` using `QAccessibleAnnouncementEvent`. The existing `announce(text, interrupt)` API stays; `interrupt=true` maps to `QAccessible::AnnouncementPoliteness::Assertive`, default maps to `Polite`.
- **ADD** new C++ helpers `announcePolite(text)` and `announceAssertive(text)` for new call sites that want to be explicit.
- **ADD** Settings sub-object property `accessibility/announcementMode` with three values:
  - `"platform"` (default for new installs) — emit `QAccessibleAnnouncementEvent` only.
  - `"tts"` — `QTextToSpeech` only (legacy behavior).
  - `"both"` — emit the platform event **and** speak via TTS (diagnostics; documented as "may overlap with screen reader").
- **ADD** one-shot migration: existing installs with `ttsEnabled == true` get `announcementMode = "both"` so they don't go silent. New installs default to `"platform"`.
- **MODIFY** `AccessibilityManager::announce()` to dispatch per the chosen mode. **No QML call-site changes.**
- **ADD** Settings → Accessibility UI: mode picker, hint label clarifying "platform mode requires an active screen reader", and a "Test announcement" button that fires both Polite and Assertive samples in the currently-selected mode.
- **ADD** observability: log every announcement (text + chosen path) and every mode change (old → new) via the existing async logger.

### Out of scope (explicit)

- High-contrast theme support (`QStyleHints::accessibility()->contrastPreference` integration with `Theme.qml`) — deferred; tracked separately.
- Migrating individual QML call sites to call the politeness-specific helpers directly — most are fine on the default mapping.
- Adopting QML-native `Accessible.announce()` at call sites — centralization through `AccessibilityManager` is intentional.
- Accessibility attribute (key/value) reporting on the live shot graph — separate change.
- The Qt 6.10 audit pass (TalkBack/VoiceOver sweep for Quick Controls re-exposure freebies) — small enough to do as a follow-up issue, not a spec change.

## Impact

- **Affected specs**: new `accessibility-announcements` capability with one requirement (platform-routed announcements) plus an observability requirement.
- **Affected code**:
  - `src/core/accessibilitymanager.h` / `.cpp` — event delivery, mode switching, migration, virtual test seams.
  - `src/core/settings_accessibility.h` / `.cpp` — `announcementMode` property (per Settings architecture: lives on the domain sub-object, not on `Settings`; `qmlRegisterUncreatableType` registration if not already present).
  - `qml/pages/settings/SettingsAccessibilityTab.qml` — mode picker + hint + Test button.
  - `docs/CLAUDE_MD/ACCESSIBILITY.md` — make platform announcements the documented primary path.
- **No QML announcement call-site changes** — the existing ~25 `AccessibilityManager.announce(...)` calls keep working.
- **Risks**: Android `View.announceForAccessibility` can drop announcements during window transitions; root `QQuickWindow` target may be unavailable during very early startup or shutdown. Both addressed in `design.md`.
