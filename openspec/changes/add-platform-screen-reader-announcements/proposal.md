# Change: Route accessibility announcements through the platform screen reader

## Why

`AccessibilityManager::announce()` currently speaks every announcement through `QTextToSpeech` directly. On Android/iOS — where TalkBack/VoiceOver are active — our TTS engine talks **in parallel with** the OS screen reader, overlapping its utterances and confusing the user. When TalkBack is off, our TTS still speaks (gated only by our `ttsEnabled` setting), which is the opposite of what an a11y-aware app should be doing.

Qt 6.8+ exposes `QAccessibleAnnouncementEvent` (QML `Accessible.announce()`), which routes through the OS accessibility framework with Polite/Assertive politeness so periodic extraction announcements don't interrupt a swipe-read. We're already on Qt 6.10.3, so the API is available — we just don't use it.

This change makes the platform screen reader the primary announcement channel **whenever one is active** and falls back to `QTextToSpeech` only when no screen reader is detected. There is no new user-facing setting: the existing `ttsEnabled` toggle keeps its current meaning — "speak via Decenza's TTS engine when no screen reader is on."

## Scope notes

This change was originally bundled with a high-contrast theme adaptation (Qt 6.10's `QStyleHints::accessibility()->contrastPreference`). The high-contrast work has been deferred — there's no current evidence of demand, the visual surface is large, and "what does Decenza look like in high contrast" needs design input that isn't engineering scope. The design notes for that work are preserved in a tracking issue for future pickup.

## What Changes

- **REPLACE** the body of `AccessibilityManager::announce(text, interrupt)` with auto-detect routing. Same signature, new behavior:
  - If `QAccessible::isActive()` returns true (TalkBack/VoiceOver/Narrator running) → dispatch a `QAccessibleAnnouncementEvent` to the root `QQuickWindow`. **Do not** also speak via `QTextToSpeech`, even if `ttsEnabled` is true. This is the fix for the overlap bug.
  - If `QAccessible::isActive()` returns false → speak via `QTextToSpeech` if `ttsEnabled` is true; otherwise stay silent. (Preserves the "spoken extraction progress without TalkBack" use case for sighted users.)
- **ADD** politeness mapping: `announce(text)` → `Polite`, `announce(text, true)` (interrupt) → `Assertive`. No QML call-site changes; the existing `interrupt` parameter is the politeness selector.
- **ADD** new C++/QML helpers `announcePolite(text)` and `announceAssertive(text)` for new call sites that want to be explicit.
- **ADD** observability: log every announcement (text + chosen path: `"platform"` / `"tts"` / `"silent"` / `"dropped"`) via the existing async logger so user reports of "missed announcements" are debuggable. Log the `QAccessible::isActive()` result alongside.
- **NO new setting.** No migration. No UI changes. The existing `ttsEnabled` toggle keeps its place in Settings; its meaning narrows from "always speak" to "speak when no screen reader is detected." Existing QML call sites and toggle bindings are unchanged.

### Out of scope (explicit)

- A user-visible mode picker (platform / tts / both). Auto-detect is sufficient; the mode picker added complexity for a setting most users would never touch. Documented in design.md as a future fallback if `QAccessible::isActive()` proves unreliable in the field.
- High-contrast theme support (`QStyleHints::accessibility()->contrastPreference` integration with `Theme.qml`) — deferred; tracked separately.
- Migrating individual QML call sites to call the politeness-specific helpers directly — most are fine on the default mapping.
- Adopting QML-native `Accessible.announce()` at call sites — centralization through `AccessibilityManager` is intentional.
- Accessibility attribute (key/value) reporting on the live shot graph — separate change.
- The Qt 6.10 audit pass (TalkBack/VoiceOver sweep for Quick Controls re-exposure freebies) — small enough to do as a follow-up issue, not a spec change.

## Impact

- **Affected specs**: new `accessibility-announcements` capability with one requirement (auto-detected platform-routed announcements) plus an observability requirement.
- **Affected code**:
  - `src/core/accessibilitymanager.h` / `.cpp` — event delivery, isActive() gate, virtual test seams.
  - `docs/CLAUDE_MD/ACCESSIBILITY.md` — make platform announcements the documented primary path; document the auto-detect routing.
- **No QML changes.** No settings UI changes. The existing ~25 `AccessibilityManager.announce(...)` calls keep working.
- **Risks**: `QAccessible::isActive()` can lag during screen-reader on/off transitions on Android; root `QQuickWindow` may be unavailable during very early startup or shutdown. Both addressed in `design.md`.
