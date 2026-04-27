# Design: Platform screen reader announcement routing

## Context

`AccessibilityManager` was written before Qt's QML `Accessible.announce()` existed (added in Qt 6.8). It uses `QTextToSpeech` directly, which:
1. Speaks even when TalkBack/VoiceOver is off (gated only by our `ttsEnabled` setting).
2. Speaks **in addition** to the OS screen reader when one is active — both voices overlap.
3. Doesn't participate in the screen reader's queue, so a Polite announcement can't wait for the user's swipe-read to finish.

This change addresses (1)–(3) by routing announcements through `QAccessibleAnnouncementEvent` whenever a platform screen reader is detected, and falling back to the existing `QTextToSpeech` path otherwise.

The originally-proposed high-contrast theme adaptation has been split out — see "Deferred work" at the end.

---

## Decisions

### 1. Auto-detect at announce time, not a user-visible mode

An earlier draft of this change added a three-value `accessibility/announcementMode` setting (`platform` / `tts` / `both`) plus a Settings UI picker, hint text, and a "Test announcement" button. We dropped that in favor of auto-detection because:

- The setting was effectively asking the user "is a screen reader running?" — which we can answer ourselves via `QAccessible::isActive()`.
- Most users would never touch it, but every user would see three new controls in the accessibility tab.
- The "both" value only existed as a diagnostic for misbehavior we would rather just observe in logs.
- Migration logic (read legacy `ttsEnabled`, set initial mode, mark migrated) was non-trivial for a setting that didn't need to exist.

The replacement design is one `if (QAccessible::isActive())` check inside `announce()`. Trade-off: cross-platform reliability of `isActive()` (see Risks below). Mitigation: every announcement re-queries — there's no cache that can go stale.

### 2. Centralize through `AccessibilityManager`, don't call `Accessible.announce()` from QML directly

We have ~25 existing call sites of `AccessibilityManager.announce(...)`. Keeping the API surface identical means:
- No widespread QML changes (low blast radius for review).
- One place to gate, log, and de-duplicate (we already track `lastAnnouncedItem`).
- One place to add diagnostics (e.g. log every announcement to the web debug logger).

**Trade-off**: Slightly less idiomatic than scattering `Accessible.announce()` across QML, but consistent with our existing pattern.

### 3. The announcement target

`QAccessibleAnnouncementEvent` needs a `QObject*` target whose accessibility interface emits the event. Two candidates:

- **Root `QQuickWindow`** — exposed via `QGuiApplication::topLevelWindows()`. Works on all platforms but requires us to fish it out at announce time.
- **The currently focused QML item** — semantically nicest (announcement is "anchored" to the focused control) but unstable; if focus is on a non-accessible item we'd silently drop.

**Decision**: use the root window. It's what Qt's own internal `Accessible.announce()` does behind the scenes, and it's robust to focus state.

Two edge cases to handle in the dispatch path:

- **Empty top-level windows during very early startup or final shutdown.** `AccessibilityManager.announce()` is callable from anywhere; if `topLevelWindows()` is empty (splash screen not yet shown, or main window already destroyed), the dispatcher MUST null-guard and silently drop without crashing. Log at debug level so we can spot it in transcripts.
- **Mid-navigation announcements (`pageStack.busy`).** Phase-change announcements often fire while the StackView is transitioning. Android's accessibility bridge can drop announcements whose target window is mid-transition. Mitigation: log when delivery happens during `pageStack.busy` so we can correlate with user reports of "missed" announcements.

### 4. Politeness mapping

| Existing call | New politeness |
|---------------|----------------|
| `announce(text)` | `Polite` |
| `announce(text, true)` (interrupt) | `Assertive` |

This preserves caller intent without changing any QML.

### 5. Routing rule (the whole behavior)

```
on announce(text, interrupt):
  if not enabled OR shutting down: drop
  if QAccessible::isActive():
    dispatch QAccessibleAnnouncementEvent (Polite/Assertive per interrupt)
    do NOT speak via QTextToSpeech, even if ttsEnabled is true
  else:
    if ttsEnabled:
      QTextToSpeech::say(text), with stop() first if interrupt
    else:
      stay silent
```

Notes:

- `ttsEnabled` keeps its existing meaning ("speak via Decenza TTS"), but now only matters when no screen reader is detected. Sighted users who enable it for spoken extraction progress (no TalkBack) keep that capability unchanged.
- When a screen reader IS detected, we suppress TTS unconditionally — that's the bug fix. There is no "both" mode in the new design.
- `lastAnnouncedItem` de-duplication runs on every path (platform and TTS) — its semantics don't change.

### 6. No migration

Because we're not introducing a new setting, there is nothing to migrate. The legacy `accessibility/ttsEnabled` key is already in the right shape; it just gains a slightly narrower runtime meaning.

---

## Risks

- **`QAccessible::isActive()` lag on Android**: Qt's accessibility bridge can take a moment to reflect a TalkBack on/off transition. Worst case: the user toggles TalkBack and the next one or two announcements take the wrong path before `isActive()` catches up. Acceptable — the user retoggling is an unusual mid-session event, and the system self-corrects on subsequent announcements.
- **iOS backgrounded extraction**: extraction announcements fire periodically; iOS may suppress accessibility announcements when backgrounded. Acceptable.
- **Screen reader claimed-but-misbehaving**: if `isActive()` returns true but the platform fails to actually deliver (Android `View.announceForAccessibility` is best-effort and can drop during UI transitions), the user hears nothing. Mitigation: log the chosen path on every announcement so user reports of "missed" announcements can be reproduced from transcripts. If this turns out to bite real users, we can revisit and add a "force TTS fallback" hidden setting — but only with field evidence.
- **No automated a11y test coverage**: implementation tasks include manual TalkBack and VoiceOver verification on hardware before merge. Virtual `dispatchPlatformAnnouncement` / `dispatchTtsAnnouncement` provide a unit-test seam to at least verify mode-routing logic without touching real screen readers.

---

## Deferred work

The original proposal also bundled high-contrast theme adaptation via Qt 6.10's `QStyleHints::accessibility()->contrastPreference`. That has been split out for the following reasons:

- No current evidence of user demand for high-contrast support.
- Touching every getter in `Theme.qml` is high blast radius for visual regressions, and we have no automated visual testing.
- "What does Decenza look like in high contrast" is a designer question (palette choices, what 'high contrast' means for the brand), not just an engineering one.
- The `readonly property color foo: ...` pattern that pervades `Theme.qml` doesn't re-evaluate on NOTIFY signals — adopting it would mean a sweep of every readonly conversion plus thorough visual verification.

The technical design (tri-state `Qt::ContrastPreference`, `NoPreference` maps to normal, user override modes `system` / `normal` / `high`, opacity-vs-explicit-disabled-color trade-off, no-touch policy on shot graph and cup-fill colors) is captured in the tracking issue and can be picked up when there is either user-reported pain or a parallel `Theme.qml` rework.
