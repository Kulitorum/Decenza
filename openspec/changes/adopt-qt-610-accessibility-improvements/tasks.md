# Tasks

## 1. Settings sub-object plumbing

- [ ] 1.1 Add `announcementMode` (QString, values: `"platform"` | `"tts"` | `"both"`) to `SettingsAccessibility` with `Q_PROPERTY` + NOTIFY signal.
- [ ] 1.2 Add `contrastMode` (QString, values: `"system"` | `"normal"` | `"high"`) to `SettingsAccessibility`.
- [ ] 1.3 Persist both through `QSettings` keys `accessibility/announcementMode` and `accessibility/contrastMode`.
- [ ] 1.4 If `SettingsAccessibility` isn't already registered with `qmlRegisterUncreatableType`, register it in `main.cpp` so QML can read `Settings.accessibility.announcementMode` etc.
- [ ] 1.5 One-shot migration in `SettingsAccessibility` constructor: if `ttsEnabled == true` and `announcementMode` key absent → set `"both"`; otherwise → `"platform"`. Set a separate `accessibility/announcementMode_migrated_v1` flag so the migration only runs once. (The `_v1` suffix lets future schema migrations slot in cleanly without re-running this one.)

## 2. Announcement event delivery

- [ ] 2.1 Add `announcePolite(const QString&)` and `announceAssertive(const QString&)` to `AccessibilityManager`.
- [ ] 2.2 Implement a private **virtual** `dispatchPlatformAnnouncement(text, politeness)` (virtual for the test seam in 7.2) that locates the root `QQuickWindow` via `QGuiApplication::topLevelWindows()`, builds a `QAccessibleAnnouncementEvent`, and posts it via `QAccessible::updateAccessibility()`.
- [ ] 2.3 Null-guard the dispatcher: if `topLevelWindows()` is empty (very early startup or shutdown), return without crashing and log at debug level. Same for the case where the first window is not a `QQuickWindow`.
- [ ] 2.4 Refactor `AccessibilityManager::announce(text, interrupt)` to consult `Settings.accessibility.announcementMode` and dispatch:
  - `"platform"` → only `dispatchPlatformAnnouncement(text, interrupt ? Assertive : Polite)`
  - `"tts"` → only existing `m_tts->say()` path
  - `"both"` → both paths
- [ ] 2.5 Verify the existing `lastAnnouncedItem` de-duplication still applies regardless of mode.
- [ ] 2.6 Log every announcement (text + chosen path) to the existing async logger so we can diagnose dropped announcements on device. Also log when `announcementMode` itself changes (with old → new) so transcripts make mode-switch debugging trivial.
- [ ] 2.7 If a platform-mode dispatch fires while `pageStack.busy` is true, log at debug level — Android's a11y bridge can drop announcements during window transitions, and we want correlation data.

## 3. High-contrast theme

- [ ] 3.1 Add C++ shim `AccessibilityManager::platformContrastPreference()` returning a tri-state (mirror of `Qt::ContrastPreference`: `NoPreference` / `HighContrast` / future-reserved). Wire to `QStyleHints::accessibility()->contrastPreferenceChanged`. Treat `NoPreference` as "normal" downstream — it is a real value, not a missing one.
- [ ] 3.2 Add `AccessibilityManager::effectiveHighContrast` (Q_PROPERTY, bool, NOTIFY) combining the platform preference with the user override (`Settings.accessibility.contrastMode`): `"system"` → derived from platform preference (`HighContrast` → true, `NoPreference` → false); `"normal"` → false; `"high"` → true.
- [ ] 3.3 Define a `paletteHighContrast` block in `Theme.qml` with the three documented differences (text colors, outline width/opacity, disabled-state colors instead of opacity).
- [ ] 3.4 Bind every existing `Theme.*` color/style getter to switch on `AccessibilityManager.effectiveHighContrast`. **CRITICAL**: replace `readonly property color foo: ...` with `property color foo: highContrast ? hcValue : normalValue` — `readonly property color` captures once and will NOT re-evaluate on the NOTIFY signal. Audit every Theme.qml property converted in this task to confirm none are `readonly`.
- [ ] 3.5 Verify `Theme.qml` rebuilds the binding when the platform hint changes mid-session (e.g. user enables Increase Contrast in iOS Settings while Decenza is open). Concrete smoke test: open the Settings page, toggle the OS hint, confirm visible re-render without restart.

## 4. Settings UI

- [ ] 4.1 Audit existing Settings tabs for a reusable segmented-control component. If one exists (e.g. used by extraction announcement mode), use it. If not, fall back to a row of `AccessibleButton`s in a `RadioButton`-like grouping. **Do not introduce a new SegmentedControl component as part of this change** — that's hidden scope.
- [ ] 4.2 Add an "Announcement delivery" mode picker to `qml/pages/settings/SettingsAccessibilityTab.qml` bound to `Settings.accessibility.announcementMode`. Include a small hint label below it: *"Platform mode requires an active screen reader (TalkBack / VoiceOver). Choose 'TTS' or 'Both' for audible announcements without a screen reader."*
- [ ] 4.3 Add a "Contrast" mode picker bound to `Settings.accessibility.contrastMode`.
- [ ] 4.4 Add a "Test announcement" button that calls both `announcePolite("Polite test")` and `announceAssertive("Assertive test")` 1.5s apart. The button uses the **current** `announcementMode` so it doubles as a verification of the chosen mode.
- [ ] 4.5 All new controls follow the accessibility rules in `docs/CLAUDE_MD/ACCESSIBILITY.md` (`Accessible.role`, `Accessible.name`, `activeFocusOnTab`, `KeyNavigation`).
- [ ] 4.6 All new strings use `TranslationManager.translate(...)`; reuse existing common keys where possible.

## 5. Documentation

- [ ] 5.1 Update `docs/CLAUDE_MD/ACCESSIBILITY.md`:
  - Mark `Accessible.announce()` / `QAccessibleAnnouncementEvent` as the primary delivery path.
  - Mark `QTextToSpeech` direct usage as fallback only.
  - Document the new `announcementMode` and `contrastMode` settings.
  - Document the high-contrast palette and its scope (Theme colors only — graphs/cup unchanged).
- [ ] 5.2 Add a one-line note to `docs/CLAUDE_MD/SETTINGS.md` if the two new properties are non-obvious examples for readers.

## 6. Audit (Qt 6.10 freebies)

- [ ] 6.1 Run a TalkBack pass on the Decent tablet through the major flows (Idle, Espresso, Steam, HotWater, Settings). Note any controls whose announcement quality changed (positive or regression) since the Qt 6.10 bump.
- [ ] 6.2 Run a VoiceOver pass on iPad through the same flows.
- [ ] 6.3 Identify any of our own a11y workarounds (added before 6.10) that became redundant. Remove them in-place if encountered. Open a follow-up issue listing anything bigger.
- [ ] 6.4 Note any Qt 6.10 a11y regressions; file upstream if reproducible.

## 7. Verification

- [ ] 7.1 Build all platforms (Windows, macOS, iOS, Android) — no new warnings.
- [ ] 7.2 Unit test: switching `announcementMode` at runtime changes the dispatch path on the next `announce()` call. **Test seam**: subclass `AccessibilityManager` overriding the virtual `dispatchPlatformAnnouncement(text, politeness)` to record calls into a vector instead of posting to QAccessible. Same approach for the TTS path (a virtual `dispatchTtsAnnouncement(text, interrupt)`). Make both virtual in `accessibilitymanager.h` as part of task 2.2.
- [ ] 7.3 Manual TalkBack test on Android: `"platform"` mode — every announcement comes through TalkBack only; `"tts"` — TTS only; `"both"` — both speak.
- [ ] 7.4 Manual VoiceOver test on iPad: same matrix.
- [ ] 7.5 Manual high-contrast verification: enable Windows 11 Contrast theme + iOS Increase Contrast + Android High Contrast Text; confirm `Theme.qml` switches in each case and reverts when disabled.
- [ ] 7.6 Migration test: launch a build with the legacy `ttsEnabled = true` setting and confirm `announcementMode` is migrated to `"both"` exactly once.
- [ ] 7.7 Confirm none of the existing 25 `AccessibilityManager.announce(...)` call sites required modification.
