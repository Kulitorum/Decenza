# Design: Platform screen reader announcement routing

## Context

`AccessibilityManager` was written before Qt's QML `Accessible.announce()` existed (added in Qt 6.8). It uses `QTextToSpeech` directly, which:
1. Speaks even when TalkBack/VoiceOver is off (gated only by our `ttsEnabled` setting).
2. Speaks **in addition** to the OS screen reader when one is active — both voices overlap.
3. Doesn't participate in the screen reader's queue, so a Polite announcement can't wait for the user's swipe-read to finish.

This change addresses (1)–(3) by routing announcements through `QAccessibleAnnouncementEvent`.

The originally-proposed high-contrast theme adaptation has been split out — see "Deferred work" at the end.

---

## Decisions

### 1. Centralize through `AccessibilityManager`, don't call `Accessible.announce()` from QML directly

We have ~25 existing call sites of `AccessibilityManager.announce(...)`. Keeping the API surface identical means:
- No widespread QML changes (low blast radius for review).
- One place to gate, log, and de-duplicate (we already track `lastAnnouncedItem`).
- One place to add diagnostics (e.g. log every announcement to the web debug logger).

**Trade-off**: Slightly less idiomatic than scattering `Accessible.announce()` across QML, but consistent with our existing pattern.

### 2. The announcement target

`QAccessibleAnnouncementEvent` needs a `QObject*` target whose accessibility interface emits the event. Two candidates:

- **Root `QQuickWindow`** — exposed via `QGuiApplication::topLevelWindows()`. Works on all platforms but requires us to fish it out at announce time.
- **The currently focused QML item** — semantically nicest (announcement is "anchored" to the focused control) but unstable; if focus is on a non-accessible item we'd silently drop.

**Decision**: use the root window. It's what Qt's own internal `Accessible.announce()` does behind the scenes, and it's robust to focus state.

Two edge cases to handle in the dispatch path:

- **Empty top-level windows during very early startup or final shutdown.** `AccessibilityManager.announce()` is callable from anywhere; if `topLevelWindows()` is empty (splash screen not yet shown, or main window already destroyed), the dispatcher MUST null-guard and silently drop without crashing. Log at debug level so we can spot it in transcripts.
- **Mid-navigation announcements (`pageStack.busy`).** Phase-change announcements often fire while the StackView is transitioning. Android's accessibility bridge can drop announcements whose target window is mid-transition. Mitigation: log when delivery happens during `pageStack.busy` so we can correlate with user reports of "missed" announcements; document `"both"` mode as the workaround if this turns out to bite real users.

### 3. Politeness mapping

| Existing call | New politeness |
|---------------|----------------|
| `announce(text)` | `Polite` |
| `announce(text, true)` (interrupt) | `Assertive` |

This preserves caller intent without changing any QML.

### 4. Three-mode policy

`accessibility/announcementMode` with three values:

- `"platform"` (default for new installs) — emit `QAccessibleAnnouncementEvent`. **No `QTextToSpeech` fallback.** If the user has no screen reader running they hear nothing. This matches every other a11y-aware Qt app.
- `"tts"` — legacy behavior: speak via `QTextToSpeech` only. For users who want speech without enabling TalkBack.
- `"both"` — emit the platform event **and** speak via `QTextToSpeech`. Diagnostics only; documented as "may overlap with screen reader".

We do NOT auto-detect screen reader presence and switch modes. Detection is unreliable cross-platform (Qt offers `QAccessible::isActive()` but it can lag, especially on Android). Explicit user choice is more predictable.

### 5. Migration & defaults

Existing installs have `ttsEnabled = true` from the legacy implementation. To avoid a silent regression for current users:

- On first run after upgrade: if `ttsEnabled == true` and `announcementMode` is unset, set it to `"both"` (preserving audible speech) and surface a one-time toast pointing to the new setting.
- New installs: `"platform"`.

This is a deliberate one-time migration, not a long-lived compatibility shim.

---

## Risks

- **Android `View.announceForAccessibility` latency**: best-effort; can drop announcements during certain UI transitions. `QTextToSpeech` is more reliable but wrong-headed. Mitigation: the `"both"` fallback is available.
- **iOS backgrounded extraction**: extraction announcements fire periodically; iOS may suppress accessibility announcements when backgrounded. Acceptable.
- **No automated a11y test coverage**: implementation tasks include manual TalkBack and VoiceOver verification on hardware before merge. Virtual `dispatchPlatformAnnouncement` / `dispatchTtsAnnouncement` provide a unit-test seam to at least verify mode-routing logic without touching real screen readers.

---

## Deferred work

The original proposal also bundled high-contrast theme adaptation via Qt 6.10's `QStyleHints::accessibility()->contrastPreference`. That has been split out for the following reasons:

- No current evidence of user demand for high-contrast support.
- Touching every getter in `Theme.qml` is high blast radius for visual regressions, and we have no automated visual testing.
- "What does Decenza look like in high contrast" is a designer question (palette choices, what 'high contrast' means for the brand), not just an engineering one.
- The `readonly property color foo: ...` pattern that pervades `Theme.qml` doesn't re-evaluate on NOTIFY signals — adopting it would mean a sweep of every readonly conversion plus thorough visual verification.

The technical design (tri-state `Qt::ContrastPreference`, `NoPreference` maps to normal, user override modes `system` / `normal` / `high`, opacity-vs-explicit-disabled-color trade-off, no-touch policy on shot graph and cup-fill colors) is captured in the tracking issue and can be picked up when there is either user-reported pain or a parallel `Theme.qml` rework.
