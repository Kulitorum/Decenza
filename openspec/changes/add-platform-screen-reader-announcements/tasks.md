# Tasks

## 1. Settings sub-object plumbing

- [ ] 1.1 Add `announcementMode` (QString, values: `"platform"` | `"tts"` | `"both"`) to `SettingsAccessibility` with `Q_PROPERTY` + NOTIFY signal.
- [ ] 1.2 Persist through `QSettings` key `accessibility/announcementMode`.
- [ ] 1.3 If `SettingsAccessibility` isn't already registered with `qmlRegisterUncreatableType`, register it in `main.cpp` so QML can read `Settings.accessibility.announcementMode`.
- [ ] 1.4 One-shot migration in `SettingsAccessibility` constructor: if `ttsEnabled == true` and `announcementMode` key absent → set `"both"`; otherwise → `"platform"`. Set a separate `accessibility/announcementMode_migrated_v1` flag so the migration only runs once. (The `_v1` suffix lets future schema migrations slot in cleanly without re-running this one.)

## 2. Announcement event delivery

- [ ] 2.1 Add `announcePolite(const QString&)` and `announceAssertive(const QString&)` to `AccessibilityManager`.
- [ ] 2.2 Implement private **virtual** `dispatchPlatformAnnouncement(text, politeness)` and `dispatchTtsAnnouncement(text, interrupt)` (virtual so unit tests can override — see 7.2). Platform dispatcher locates the root `QQuickWindow` via `QGuiApplication::topLevelWindows()`, builds a `QAccessibleAnnouncementEvent`, and posts it via `QAccessible::updateAccessibility()`.
- [ ] 2.3 Null-guard the dispatcher: if `topLevelWindows()` is empty (very early startup or shutdown), return without crashing and log at debug level. Same for the case where the first window is not a `QQuickWindow`.
- [ ] 2.4 Refactor `AccessibilityManager::announce(text, interrupt)` to consult `Settings.accessibility.announcementMode` and dispatch:
  - `"platform"` → only `dispatchPlatformAnnouncement(text, interrupt ? Assertive : Polite)`
  - `"tts"` → only `dispatchTtsAnnouncement(text, interrupt)`
  - `"both"` → both paths
- [ ] 2.5 Verify the existing `lastAnnouncedItem` de-duplication still applies regardless of mode.
- [ ] 2.6 Log every announcement (text + chosen path) to the existing async logger so we can diagnose dropped announcements on device. Also log when `announcementMode` itself changes (with old → new) so transcripts make mode-switch debugging trivial.
- [ ] 2.7 If a platform-mode dispatch fires while `pageStack.busy` is true, log at debug level — Android's a11y bridge can drop announcements during window transitions, and we want correlation data.

## 3. Settings UI

- [ ] 3.1 Audit existing Settings tabs for a reusable mode-picker / segmented-control component. If one exists (e.g. used by extraction announcement mode), use it. If not, fall back to a row of `AccessibleButton`s in a `RadioButton`-like grouping. **Do not introduce a new SegmentedControl component as part of this change** — that's hidden scope.
- [ ] 3.2 Add an "Announcement delivery" mode picker to `qml/pages/settings/SettingsAccessibilityTab.qml` bound to `Settings.accessibility.announcementMode`. Include a small hint label below it: *"Platform mode requires an active screen reader (TalkBack / VoiceOver). Choose 'TTS' or 'Both' for audible announcements without a screen reader."*
- [ ] 3.3 Add a "Test announcement" button that calls both `announcePolite("Polite test")` and `announceAssertive("Assertive test")` 1.5s apart. The button uses the **current** `announcementMode` so it doubles as a verification of the chosen mode.
- [ ] 3.4 All new controls follow the accessibility rules in `docs/CLAUDE_MD/ACCESSIBILITY.md` (`Accessible.role`, `Accessible.name`, `activeFocusOnTab`, `KeyNavigation`).
- [ ] 3.5 All new strings use `TranslationManager.translate(...)`; reuse existing common keys where possible.

## 4. Documentation

- [ ] 4.1 Update `docs/CLAUDE_MD/ACCESSIBILITY.md`:
  - Mark `Accessible.announce()` / `QAccessibleAnnouncementEvent` as the primary delivery path.
  - Mark `QTextToSpeech` direct usage as a fallback only (selected by user via mode setting).
  - Document the new `announcementMode` setting and migration behavior.

## 5. Verification

- [ ] 5.1 Build all platforms (Windows, macOS, iOS, Android) — no new warnings.
- [ ] 5.2 Unit test: switching `announcementMode` at runtime changes the dispatch path on the next `announce()` call. **Test seam**: subclass `AccessibilityManager` overriding the virtual `dispatchPlatformAnnouncement` and `dispatchTtsAnnouncement` to record calls into a vector instead of posting to QAccessible / TTS.
- [ ] 5.3 Manual TalkBack test on Android: `"platform"` mode — every announcement comes through TalkBack only; `"tts"` — TTS only; `"both"` — both speak.
- [ ] 5.4 Manual VoiceOver test on iPad: same matrix.
- [ ] 5.5 Migration test: launch a build with the legacy `ttsEnabled = true` setting and confirm `announcementMode` is migrated to `"both"` exactly once.
- [ ] 5.6 Confirm none of the existing ~25 `AccessibilityManager.announce(...)` call sites required modification.
