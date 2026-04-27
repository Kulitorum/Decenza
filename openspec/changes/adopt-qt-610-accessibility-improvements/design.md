# Design: Adopt Qt 6.10 accessibility improvements

## Context

`AccessibilityManager` was written before Qt's QML `Accessible.announce()` existed (added in Qt 6.8). It uses `QTextToSpeech` directly, which:
1. Speaks even when TalkBack/VoiceOver is off (gated only by our `ttsEnabled` setting).
2. Speaks **in addition** to the OS screen reader when one is active — both voices overlap.
3. Doesn't participate in the screen reader's queue, so a Polite announcement can't wait for the user's swipe-read to finish.

`Theme.qml` is the single source of truth for color/font/spacing. Qt 6.10's built-in styles now respect a platform high-contrast hint, but our custom theme bypasses those styles entirely and therefore needs to wire up to the same hint manually.

This change addresses both: (A) announcements via `QAccessibleAnnouncementEvent`, and (B) high-contrast palette via `QStyleHints`.

---

## A. Announcement routing

### A.1 — Centralize through `AccessibilityManager`, don't call `Accessible.announce()` from QML directly

We have ~25 existing call sites of `AccessibilityManager.announce(...)`. Keeping the API surface identical means:
- No widespread QML changes (low blast radius for review).
- One place to gate, log, and de-duplicate (we already track `lastAnnouncedItem`).
- One place to add diagnostics (e.g. log every announcement to the web debug logger).

**Trade-off**: Slightly less idiomatic than scattering `Accessible.announce()` across QML, but consistent with our existing pattern.

### A.2 — The announcement target

`QAccessibleAnnouncementEvent` needs a `QObject*` target whose accessibility interface emits the event. Two candidates:

- **Root `QQuickWindow`** — exposed via `QGuiApplication::topLevelWindows()`. Works on all platforms but requires us to fish it out at announce time.
- **The currently focused QML item** — semantically nicest (announcement is "anchored" to the focused control) but unstable; if focus is on a non-accessible item we'd silently drop.

**Decision**: use the root window. It's what Qt's own internal `Accessible.announce()` does behind the scenes, and it's robust to focus state.

Two edge cases to handle in the dispatch path:

- **Empty top-level windows during very early startup or final shutdown.** `AccessibilityManager.announce()` is callable from anywhere; if `topLevelWindows()` is empty (splash screen not yet shown, or main window already destroyed), the dispatcher MUST null-guard and silently drop without crashing. Log at debug level so we can spot it in transcripts.
- **Mid-navigation announcements (`pageStack.busy`).** Phase-change announcements often fire while the StackView is transitioning. Android's accessibility bridge can drop announcements whose target window is mid-transition. Mitigation: log when delivery happens during `pageStack.busy` so we can correlate with user reports of "missed" announcements; document `"both"` mode as the workaround if this turns out to bite real users.

### A.3 — Politeness mapping

| Existing call | New politeness |
|---------------|----------------|
| `announce(text)` | `Polite` |
| `announce(text, true)` (interrupt) | `Assertive` |

This preserves caller intent without changing any QML.

### A.4 — Three-mode policy

`accessibility/announcementMode` with three values:

- `"platform"` (default for new installs) — emit `QAccessibleAnnouncementEvent`. **No `QTextToSpeech` fallback.** If the user has no screen reader running they hear nothing. This matches every other a11y-aware Qt app.
- `"tts"` — legacy behavior: speak via `QTextToSpeech` only. For users who want speech without enabling TalkBack.
- `"both"` — emit the platform event **and** speak via `QTextToSpeech`. Diagnostics only; documented as "may overlap with screen reader".

We do NOT auto-detect screen reader presence and switch modes. Detection is unreliable cross-platform (Qt offers `QAccessible::isActive()` but it can lag, especially on Android). Explicit user choice is more predictable.

### A.5 — Migration & defaults

Existing installs have `ttsEnabled = true` from the legacy implementation. To avoid a silent regression for current users:

- On first run after upgrade: if `ttsEnabled == true` and `announcementMode` is unset, we set it to `"both"` (preserving audible speech) and surface a one-time toast pointing to the new setting.
- New installs: `"platform"`.

This is a deliberate one-time migration, not a long-lived compatibility shim.

---

## B. High-contrast theme

### B.1 — Hint source

Qt 6.10 exposes the platform contrast preference via `QStyleHints::accessibility()` → `QAccessibilityHints*`, which carries a `Qt::ContrastPreference` property (`NoPreference`, `HighContrast`, plus room for future values) and a `contrastPreferenceChanged(Qt::ContrastPreference)` signal. The plan:

1. Read the value at startup and connect to `contrastPreferenceChanged` (no polling needed).
2. Expose it to QML via `AccessibilityManager::platformContrastPreference()` returning a mirrored enum (or `Qt::ContrastPreference` directly) plus a notifying `Q_PROPERTY`. We wrap it (rather than letting QML reach into `QStyleHints` directly) so we can override during testing and so a future Qt rename is a one-line fix.
3. `Theme.qml` reads `AccessibilityManager.effectiveContrastPreference` (combined with the user override) to choose between `paletteNormal` and `paletteHighContrast`.

`NoPreference` is a real value, not "missing" — it means the platform exposes the API but the user has not opted in. Treat it as "normal" in our palette mapping. This matters because `contrastMode = "system"` on a platform that returns `NoPreference` (most users) must reliably fall through to the normal palette.

### B.2 — What "high contrast" means in Theme.qml

Three concrete differences in the high-contrast palette:

| Aspect | Normal | High contrast |
|--------|--------|---------------|
| Body text on background | Theme grey on dark | Pure white on near-black |
| Button outline | Subtle 1px at 30% opacity | 2px solid at 100% |
| Disabled state | 50% opacity dim | Explicit lower-saturation color (no opacity) |

Opacity-based dimming is a well-known a11y problem because reduced-contrast users may have set their OS to amplify contrast. Replacing opacity with explicit colors is the high-contrast win, not just "make everything brighter".

### B.3 — User override

Three values for `accessibility/contrastMode`:
- `"system"` (default) — follow OS hint.
- `"normal"` — force normal palette regardless of OS.
- `"high"` — force high-contrast palette regardless of OS.

The override exists because (1) some platforms don't expose the hint (older Android), (2) users on shared devices may want app-only high contrast.

### B.4 — Scope of the palette swap

Only the colors used by `Theme.qml` change. We do NOT touch:
- Shot graph rendering colors (those are domain-meaningful — pressure is always blue).
- Cup fill view shaders (visual fidelity, not text legibility).
- Emoji rendering (passes through unchanged).

If a high-contrast user needs the shot graph to be more legible, that's a separate change.

---

## Risks

- **Android `View.announceForAccessibility` latency**: best-effort; can drop announcements during certain UI transitions. `QTextToSpeech` is more reliable but wrong-headed. Mitigation: the `"both"` fallback is available.
- **iOS backgrounded extraction**: extraction announcements fire periodically; iOS may suppress accessibility announcements when backgrounded. Acceptable.
- **Qt 6.10 contrast hint API stability**: the property name on `QStyleHints` is new in 6.10. Wrap behind `AccessibilityManager` so a future Qt rename is a one-line fix, not a sweep through QML.
- **No automated a11y test coverage**: implementation tasks include manual TalkBack and VoiceOver verification on hardware before merge.
