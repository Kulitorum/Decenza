# accessibility-announcements

## ADDED Requirements

### Requirement: Announcements SHALL route through the platform screen reader whenever one is active

The application SHALL deliver accessibility announcements through the platform accessibility framework (TalkBack on Android, VoiceOver on iOS/macOS, Narrator/UIA on Windows) using `QAccessibleAnnouncementEvent` whenever `QAccessible::isActive()` reports a screen reader as active. In that case the application SHALL NOT additionally speak via `QTextToSpeech`, even if the user has the `ttsEnabled` toggle on. When no screen reader is detected, the application SHALL fall back to its existing `QTextToSpeech` path, gated by `ttsEnabled`.

The existing `AccessibilityManager.announce(text, interrupt)` API SHALL be preserved without changes to its signature or semantics from the caller's perspective. The `interrupt` parameter SHALL map to assertive announcement politeness; the default SHALL map to polite.

There SHALL NOT be a user-visible delivery-mode setting (e.g. a "platform / tts / both" picker). Routing is automatic based on the screen reader's active state.

#### Scenario: Active screen reader routes through TalkBack only

- **GIVEN** TalkBack is enabled on the device and `QAccessible::isActive()` returns true
- **WHEN** any QML caller invokes `AccessibilityManager.announce("Shot complete")`
- **THEN** TalkBack SHALL speak the message via the OS accessibility queue
- **AND** the application's `QTextToSpeech` engine SHALL NOT speak (regardless of the `ttsEnabled` toggle)

#### Scenario: No screen reader, TTS enabled — falls back to QTextToSpeech

- **GIVEN** no screen reader is active (`QAccessible::isActive()` returns false)
- **AND** the user has `ttsEnabled == true`
- **WHEN** any caller invokes `AccessibilityManager.announce("Shot complete")`
- **THEN** `QTextToSpeech::say(...)` SHALL speak the message
- **AND** no `QAccessibleAnnouncementEvent` SHALL be dispatched

#### Scenario: No screen reader, TTS disabled — silent

- **GIVEN** no screen reader is active and `ttsEnabled == false`
- **WHEN** any caller invokes `AccessibilityManager.announce(...)`
- **THEN** the application SHALL stay silent
- **AND** no `QAccessibleAnnouncementEvent` SHALL be dispatched

#### Scenario: Interrupt argument maps to assertive politeness

- **GIVEN** a screen reader is active
- **WHEN** a caller invokes `AccessibilityManager.announce("Error", true)`
- **THEN** the dispatched `QAccessibleAnnouncementEvent` SHALL carry assertive politeness
- **AND** the screen reader SHALL interrupt any in-progress polite utterance

#### Scenario: Announcement during empty top-level windows is silently dropped

- **GIVEN** a screen reader is active
- **AND** `QGuiApplication::topLevelWindows()` is empty (very early startup or shutdown teardown)
- **WHEN** any caller invokes `AccessibilityManager.announce(...)`
- **THEN** the application SHALL NOT crash
- **AND** SHALL log the dropped announcement at debug level so it is visible in transcripts

---

### Requirement: Announcement delivery SHALL be observable via the application log

The application SHALL log every announcement: the text content, the chosen delivery path (`"platform"`, `"tts"`, `"silent"`, or `"dropped"`), and the `QAccessible::isActive()` reading at dispatch time. Logging uses the existing async logger so it is visible in transcripts and in the web debug log.

This requirement exists because there is no automated accessibility test coverage and user reports of "missed" announcements (a real risk with platform-mode delivery on Android during window transitions, or with `QAccessible::isActive()` lag) are otherwise un-debuggable.

#### Scenario: Successful platform delivery is logged

- **GIVEN** a screen reader is active and a valid root window exists
- **WHEN** an announcement dispatches successfully
- **THEN** an entry SHALL be logged containing the announcement text, the delivery path (`"platform"`), and the `isActive()` reading

#### Scenario: TTS fallback is logged

- **GIVEN** no screen reader is active and `ttsEnabled == true`
- **WHEN** an announcement is dispatched via `QTextToSpeech`
- **THEN** an entry SHALL be logged with the text and a `"tts"` path tag

#### Scenario: Dropped announcement is logged

- **GIVEN** a screen reader is active
- **AND** `QGuiApplication::topLevelWindows()` is empty
- **WHEN** an announcement is requested
- **THEN** an entry SHALL be logged with the announcement text and a `"dropped"` path tag indicating the reason (no top-level window)
