# Tasks

## 1. Announcement event delivery

- [x] 1.1 Add `announcePolite(const QString&)` and `announceAssertive(const QString&)` Q_INVOKABLEs to `AccessibilityManager`. They call `announce(text, false)` and `announce(text, true)` respectively — thin wrappers, present so future call sites can be explicit.
- [x] 1.2 Implement protected **virtual** `dispatchPlatformAnnouncement(text, assertive)` and `dispatchTtsAnnouncement(text, interrupt)` (virtual so unit tests can override — see 3.2). Platform dispatcher locates the root `QQuickWindow` via `QGuiApplication::topLevelWindows()`, builds a `QAccessibleAnnouncementEvent`, and posts it via `QAccessible::updateAccessibility()`.
- [x] 1.3 Null-guard the platform dispatcher: if `topLevelWindows()` is empty (very early startup or shutdown), return without crashing and log at debug level. Same for the case where the first window is not a `QQuickWindow`.
- [x] 1.4 Replace the body of `AccessibilityManager::announce(text, interrupt)` with the auto-detect routing rule:
  - If `QAccessible::isActive()` is true → only `dispatchPlatformAnnouncement(text, interrupt /* assertive */)`. **Do not** also speak via TTS.
  - Else, if `m_ttsEnabled` is true → `dispatchTtsAnnouncement(text, interrupt)` (existing behavior).
  - Else → silent.
- [x] 1.5 The existing `lastAnnouncedItem` de-duplication is owned by the `setLastAnnouncedItem` slot; `announce()` neither sets nor reads it, so both routing paths inherit the existing behavior unchanged.
- [x] 1.6 Log every announcement (text + chosen path: `"platform"`, `"tts"`, `"silent"`, `"dropped"`) at qInfo level so transcripts capture user-reportable "missed announcement" cases.
- [x] 1.7 Logging the dispatch on every call gives transcripts the timing data needed to correlate platform-mode misses with window transitions; `pageStack.busy` is QML-side and was not surfaced into C++ for this change.

## 2. Documentation

- [x] 2.1 Update `docs/CLAUDE_MD/ACCESSIBILITY.md`:
  - Mark `QAccessibleAnnouncementEvent` (via `AccessibilityManager.announce(...)`) as the primary delivery path whenever a screen reader is active.
  - Document that `ttsEnabled` is now the no-screen-reader fallback path, not the unconditional speech toggle it used to be.
  - Note that there is no user-visible mode setting — routing is automatic.

## 3. Verification

- [ ] 3.1 Build all platforms (Windows, macOS, iOS, Android) — no new warnings. *(deferred to user — Qt Creator desktop build first)*
- [x] 3.2 Unit test (`tests/tst_accessibility_announcements.cpp`): subclasses `AccessibilityManager` and overrides `isScreenReaderActive`, `dispatchPlatformAnnouncement`, and `dispatchTtsAnnouncement` to record calls. Verifies:
  - `isScreenReaderActive() == true` → only platform dispatch records the call.
  - `isScreenReaderActive() == false` and `ttsEnabled == true` → only TTS dispatch records.
  - `isScreenReaderActive() == false` and `ttsEnabled == false` → neither dispatcher is called.
  - `interrupt=true` maps to assertive on the platform path and to `interrupt=true` on the TTS path.
- [ ] 3.3 Manual TalkBack test on Android: with TalkBack ON, announcements come through TalkBack only (no double-speak). With TalkBack OFF and `ttsEnabled` ON, announcements come via Decenza's TTS as before. *(manual on-device check)*
- [ ] 3.4 Manual VoiceOver test on iPad: same matrix. *(manual on-device check)*
- [x] 3.5 None of the existing ~25 `AccessibilityManager.announce(...)` call sites required modification — `announce()`'s signature is unchanged.
- [x] 3.6 No Settings UI changes were required — the `ttsEnabled` toggle in `SettingsLanguageTab.qml` is unchanged.
