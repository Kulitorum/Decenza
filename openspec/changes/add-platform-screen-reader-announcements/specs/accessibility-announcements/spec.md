# accessibility-announcements

## ADDED Requirements

### Requirement: Announcements SHALL route through the platform screen reader by default

The application SHALL deliver accessibility announcements through the platform accessibility framework (TalkBack on Android, VoiceOver on iOS/macOS, Narrator/UIA on Windows) using `QAccessibleAnnouncementEvent` (or the QML `Accessible.announce()` equivalent) as the primary delivery path. The legacy `QTextToSpeech` engine SHALL only be used when the user explicitly selects a TTS-only or combined mode.

The application SHALL expose three announcement modes via a single setting (`accessibility/announcementMode`):

- `"platform"` — emit a `QAccessibleAnnouncementEvent` only.
- `"tts"` — speak via `QTextToSpeech` only.
- `"both"` — emit the platform event AND speak via `QTextToSpeech` (diagnostic mode).

The default for new installs SHALL be `"platform"`. For existing installs upgrading from a build that used the legacy `ttsEnabled` setting, the application SHALL perform a one-time migration setting the mode to `"both"` so previously-audible announcements remain audible.

The existing `AccessibilityManager.announce(text, interrupt)` API SHALL be preserved without changes to its signature or semantics from the caller's perspective. The `interrupt` parameter SHALL map to assertive announcement politeness; the default SHALL map to polite.

#### Scenario: Default mode on a new install routes to TalkBack only

- **GIVEN** a fresh install on Android with TalkBack enabled and no prior `accessibility/announcementMode` setting
- **WHEN** any QML caller invokes `AccessibilityManager.announce("Shot complete")`
- **THEN** TalkBack SHALL speak the message via the OS accessibility queue
- **AND** the application's `QTextToSpeech` engine SHALL NOT speak

#### Scenario: Legacy install with TTS enabled is migrated to "both"

- **GIVEN** an upgraded install where the user previously had `ttsEnabled = true` and no `accessibility/announcementMode` key exists
- **WHEN** the application starts for the first time after the upgrade
- **THEN** `accessibility/announcementMode` SHALL be set to `"both"`
- **AND** the migration SHALL be marked complete so it does not run again on subsequent starts

#### Scenario: Interrupt argument maps to assertive politeness

- **GIVEN** the announcement mode is `"platform"`
- **WHEN** a caller invokes `AccessibilityManager.announce("Error", true)`
- **THEN** the dispatched `QAccessibleAnnouncementEvent` SHALL carry assertive politeness
- **AND** the screen reader SHALL interrupt any in-progress polite utterance

#### Scenario: TTS-only mode preserves legacy behavior

- **GIVEN** the user has set `announcementMode = "tts"`
- **WHEN** any announcement is requested
- **THEN** `QTextToSpeech::say()` SHALL speak the message
- **AND** no `QAccessibleAnnouncementEvent` SHALL be dispatched

#### Scenario: A user can test announcements from the Settings UI

- **GIVEN** the user is on Settings → Accessibility
- **WHEN** the user activates the "Test announcement" button
- **THEN** the application SHALL dispatch one polite test announcement and one assertive test announcement, separated by approximately 1.5 seconds
- **AND** the dispatched test announcements SHALL respect the currently selected `announcementMode` (so the button doubles as a verification that the chosen mode produces audible output for the user)

#### Scenario: Platform mode with no active screen reader is silent by design

- **GIVEN** the announcement mode is `"platform"`
- **AND** no platform screen reader (TalkBack / VoiceOver / Narrator) is active on the device
- **WHEN** any caller invokes `AccessibilityManager.announce(...)`
- **THEN** the application SHALL emit the `QAccessibleAnnouncementEvent` (no fallback to TTS)
- **AND** the user SHALL hear nothing
- **AND** the Settings UI SHALL display a hint near the mode selector explaining that platform mode requires an active screen reader and recommending TTS or Both for audible output without one

#### Scenario: Mode change applies to the next announcement without restart

- **GIVEN** the application is running with `announcementMode = "platform"`
- **WHEN** the user changes `announcementMode` to `"tts"` via the Settings UI
- **AND** any caller subsequently invokes `AccessibilityManager.announce(...)`
- **THEN** the next announcement SHALL be delivered via `QTextToSpeech` (the new mode)
- **AND** no application restart SHALL be required for the change to take effect

#### Scenario: Announcement during empty top-level windows is silently dropped

- **GIVEN** the announcement mode is `"platform"` or `"both"`
- **AND** `QGuiApplication::topLevelWindows()` is empty (very early startup or shutdown teardown)
- **WHEN** any caller invokes `AccessibilityManager.announce(...)`
- **THEN** the application SHALL NOT crash
- **AND** SHALL log the dropped announcement at debug level so it is visible in transcripts

---

### Requirement: Announcement delivery SHALL be observable via the application log

The application SHALL log every announcement (text content and chosen delivery path: `"platform"`, `"tts"`, `"both"`, or `"dropped"`) and SHALL log every change to `accessibility/announcementMode` (with old → new value). Logging uses the existing async logger so it is visible in transcripts and in the web debug log.

This requirement exists because there is no automated accessibility test coverage and user reports of "missed" announcements (a real risk with platform-mode delivery on Android during window transitions) are otherwise un-debuggable.

#### Scenario: Successful platform delivery is logged

- **GIVEN** `announcementMode = "platform"` and a valid root window
- **WHEN** an announcement dispatches successfully
- **THEN** an entry SHALL be logged containing the announcement text and the delivery path (`"platform"`)

#### Scenario: Dropped announcement is logged

- **GIVEN** `announcementMode = "platform"`
- **AND** `QGuiApplication::topLevelWindows()` is empty
- **WHEN** an announcement is requested
- **THEN** an entry SHALL be logged with the announcement text and a `"dropped"` path tag indicating the reason (no top-level window)

#### Scenario: Mode changes are logged

- **GIVEN** the user changes `announcementMode` from `"platform"` to `"both"` via Settings
- **WHEN** the change persists
- **THEN** a single log entry SHALL record the old value, the new value, and the source (user/UI vs. migration)
