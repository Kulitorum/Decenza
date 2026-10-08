# accessibility-announcements Specification

## Purpose
Governs how `AccessibilityManager.announce()` delivers spoken feedback: routing through the platform screen reader (TalkBack/VoiceOver/Narrator) via `QAccessibleAnnouncementEvent` when one is active, falling back to `QTextToSpeech` otherwise, and logging every delivery decision for debugging since there is no automated coverage.

## Requirements
### Requirement: Announcements SHALL route through the platform screen reader whenever one is active

Announcements SHALL be delivered through `QAccessibleAnnouncementEvent` whenever `QAccessible::isActive()` reports a screen reader. In that case `QTextToSpeech` SHALL NOT also speak, even when `ttsEnabled` is on. When no screen reader is detected, the application SHALL fall back to `QTextToSpeech`, gated by `ttsEnabled`. The `AccessibilityManager.announce(text, interrupt)` signature SHALL be preserved.

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

### Requirement: Delivery mode SHALL NOT be a user setting

Routing SHALL be automatic, based on the screen reader's active state. There SHALL NOT be a user-visible delivery-mode setting such as a platform / tts / both picker.

#### Scenario: No delivery picker exists

- **WHEN** the user searches the settings for an announcement delivery mode
- **THEN** no delivery-mode picker SHALL be offered

### Requirement: Announcement politeness SHALL follow the interrupt argument

The `interrupt` parameter SHALL map to assertive announcement politeness, and the default SHALL map to polite.

#### Scenario: Default announcement is polite

- **GIVEN** a screen reader is active
- **WHEN** a caller invokes `AccessibilityManager.announce("Shot complete")`
- **THEN** the dispatched `QAccessibleAnnouncementEvent` SHALL carry polite politeness

### Requirement: Announcement delivery SHALL be observable via the application log

The application SHALL log every announcement with its text, its delivery path (`"platform"`, `"tts"`, `"silent"` or `"dropped"`), and the `QAccessible::isActive()` reading at dispatch. Logging SHALL use the existing async logger so entries appear in transcripts and the web debug log.

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

