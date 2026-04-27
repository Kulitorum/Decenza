# Change: Adopt Qt 6.10 accessibility improvements

## Why

We bumped to Qt 6.10.3 but did not adopt any of the accessibility improvements that release brought. The most consequential gap is announcements: `AccessibilityManager::announce()` speaks every message through `QTextToSpeech` directly, which talks **in parallel with** TalkBack/VoiceOver when those are active (overlapping the OS screen reader) and stays silent when TalkBack is off unless the user finds our separate `ttsEnabled` toggle. Both behaviors are wrong for an a11y-aware app. Qt 6.8/6.10's `QAccessibleAnnouncementEvent` (QML `Accessible.announce()`) routes through the platform a11y bridge, which is the right primitive.

Qt 6.10 also added a platform high-contrast hint that the built-in styles automatically respect — but we render through a custom `Theme.qml` singleton, so we get none of that benefit unless we subscribe to the hint and adapt our palette. Several other 6.10 a11y items (Quick Controls/Widgets re-exposure to assistive tech, attribute reporting) come for free or are deferred.

## Qt 6.10 Accessibility Improvements: Survey and Applicability

| 6.10 improvement | Available? | How it would help Decenza | Adopted in this change? |
|---|---|---|---|
| `QAccessibleAnnouncementEvent` / QML `Accessible.announce()` (added 6.8, refined 6.10) | Yes, unused | Routes our ~25 announcement call sites through TalkBack/VoiceOver instead of overlapping them. Adds Polite/Assertive politeness so periodic extraction updates don't interrupt a swipe-read. | **Yes — primary scope** |
| Platform high-contrast hint via `QStyleHints` accessibility properties | Yes, unused | Lets `Theme.qml` switch to a higher-contrast palette automatically when the user enables Windows 11 Contrast theme, GNOME High Contrast, or Android/iOS Increase Contrast — without us building a separate setting. | **Yes — secondary scope** |
| Quick Controls / Widgets re-exposed to assistive tech | Auto from Qt bump | Our existing `StyledTextField`, `StyledComboBox`, etc. now report richer roles/states without code changes. Worth a TalkBack pass to confirm and remove any of our own a11y workarounds that became redundant. | **No — audit task only** (we just need to verify) |
| Accessibility attribute reporting (key/value pairs, added 6.8) | Yes, unused | Could enrich screen reader output for the live shot graph (`{"phase":"pouring","pressure":"9 bar","weight":"32 g"}`). High value but big surface area. | **No — deferred to a future change** |
| WebAssembly a11y improvements | Yes | Not applicable — we don't ship Wasm. | — |
| Built-in style palette alignment with OS dark/light | Auto from Qt bump | Doesn't help us — we don't use Material/Fusion/Basic palettes; we drive everything through `Theme.qml`. | — |

## What Changes

### Announcements (primary)

- **ADD** event-based announcement delivery on `AccessibilityManager` using `QAccessibleAnnouncementEvent`. The existing `announce(text, interrupt)` API stays; `interrupt=true` maps to `QAccessible::AnnouncementPoliteness::Assertive`, default maps to `Polite`.
- **ADD** new C++ helpers `announcePolite(text)` and `announceAssertive(text)` for new call sites that want to be explicit.
- **ADD** Settings sub-object property `accessibility/announcementMode` with three values:
  - `"platform"` (default for new installs) — emit `QAccessibleAnnouncementEvent` only.
  - `"tts"` — `QTextToSpeech` only (legacy behavior).
  - `"both"` — emit the platform event **and** speak via TTS (diagnostics; documented as "may overlap with screen reader").
- **ADD** one-shot migration: existing installs with `ttsEnabled == true` get `announcementMode = "both"` so they don't go silent. New installs default to `"platform"`.
- **MODIFY** `AccessibilityManager::announce()` to dispatch per the chosen mode. **No QML call-site changes.**
- **ADD** Settings → Accessibility UI: mode selector and a "Test announcement" button that fires both Polite and Assertive samples.

### High contrast (secondary)

- **ADD** `Theme.qml` listening to the platform contrast preference via `QStyleHints` (exposed through a thin C++ shim if QML can't reach the new property directly in 6.10).
- **ADD** a high-contrast palette variant in `Theme.qml` — stronger outlines, higher-contrast text, no opacity-based dimming for disabled states (replace with explicit colors).
- **ADD** a manual override setting `accessibility/contrastMode` with values `"system"` (default — follow OS), `"normal"`, `"high"`. UI sits next to the new announcement mode.

### Audit (no spec change)

- **TASK** TalkBack pass on the Decent tablet and VoiceOver pass on iPad to verify Qt 6.10's automatic Quick Controls re-exposure works for `StyledTextField`, `StyledComboBox`, `CheckBox`. Any redundant a11y workarounds we previously added are removed in-place. Findings recorded as a follow-up issue, not in this change.

### Documentation

- Update `docs/CLAUDE_MD/ACCESSIBILITY.md` to (1) make platform announcements the documented primary path, (2) document the high-contrast palette, (3) mark direct `QTextToSpeech` use as a fallback only.

### Out of scope (explicit)

- Migrating the ~25 existing QML call sites away from `AccessibilityManager.announce()` to QML-native `Accessible.announce()`. Centralization is intentional (see design.md).
- Accessibility attribute (key/value) reporting on the shot graph — separate change.
- Switching Quick Controls style away from Material — orthogonal.

## Impact

- **Affected specs**: new `accessibility` capability with two requirements (platform-routed announcements, high-contrast theme support).
- **Affected code**:
  - `src/core/accessibilitymanager.h` / `.cpp` — event delivery, mode switching, migration.
  - `src/core/settings_accessibility.h` / `.cpp` — `announcementMode` and `contrastMode` properties (per Settings architecture rules: live on the domain sub-object, not on `Settings`; require `qmlRegisterUncreatableType` registration if not already present).
  - `qml/Theme.qml` — high-contrast palette + system-hint subscription.
  - `qml/pages/settings/SettingsAccessibilityTab.qml` — two new mode selectors + Test button.
  - `docs/CLAUDE_MD/ACCESSIBILITY.md` — policy update.
- **No QML announcement call-site changes** — the existing 25 `AccessibilityManager.announce(...)` calls keep working.
- **Risks**: documented in `design.md` (announcement target object selection, Android backend latency, no automated a11y test coverage).
