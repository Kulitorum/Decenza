# accessible-date-entry Specification

## Purpose

Defines how the user enters calendar dates (bean roast / frozen / defrost) in a screen-reader-friendly, locale-aware way, and the accessibility and navigation requirements of the supporting date picker. Entry order and separator follow the host locale; the stored value is always ISO `yyyy-mm-dd`.

## Requirements

### Requirement: Date fields SHALL NOT expose an input-mask skeleton to assistive technology

Calendar date fields (roast, frozen, defrost) SHALL NOT use a Qt `inputMask`. A masked field is pre-filled with a blank-character skeleton that `displayText()` exposes to the accessibility tree, causing screen readers to announce a skeleton of blanks instead of an empty, editable field. The field SHALL start genuinely empty so the platform accessibility framework announces the field's label and current value, never a formatting skeleton.

#### Scenario: Empty date field announces label, not skeleton

- **WHEN** a screen reader (TalkBack/VoiceOver) focuses an empty roast date field
- **THEN** it SHALL announce the field's accessible name and an empty value
- **AND** it SHALL NOT announce a mask skeleton such as "blank blank blank blank dash blank blank dash blank blank"

#### Scenario: No inputMask on any date field

- **WHEN** the roast, frozen, or defrost date field is constructed
- **THEN** it SHALL NOT set the `inputMask` property in any mode (screen reader active or not)

### Requirement: Date entry order and separator SHALL follow the host locale

The displayed input order (day/month/year sequence) and the separator character SHALL be derived from the host locale's short date format (`Qt.locale().dateFormat(Locale.ShortFormat)`), not hard-coded. The system SHALL always resolve a definite order; where the OS reports no locale, it SHALL fall back to the C locale (ISO `year`, `month`, `day`). No user-facing setting for date format SHALL be added — the OS locale is the single source of truth.

#### Scenario: US locale uses month-first order

- **WHEN** the host locale's short date format is month-first (e.g. `M/d/yy`)
- **THEN** the field SHALL accept and display the date in `MM/DD/YYYY` order

#### Scenario: Day-first locale uses day-first order

- **WHEN** the host locale's short date format is day-first (e.g. `dd/MM/yyyy`)
- **THEN** the field SHALL accept and display the date in `DD/MM/YYYY` order

#### Scenario: Locale unavailable falls back to ISO order

- **WHEN** no host locale can be resolved
- **THEN** the field SHALL use year-month-day order with a `-` separator

#### Scenario: No date-format setting exists

- **WHEN** the settings UI is inspected
- **THEN** there SHALL be no user-facing control for choosing the date input order

### Requirement: The date field SHALL format progressively as the user types

The field SHALL insert the locale separator automatically as digits are entered, appending separators after the cursor position and never injecting characters ahead of the caret. Editing SHALL be permitted anywhere in the value (not forced to the end), and paste SHALL be supported.

#### Scenario: Separators appear as digits are typed

- **WHEN** the user types digits into an empty field whose locale order is `MM/DD/YYYY`
- **THEN** the separator `/` SHALL be inserted automatically after the month and day segments as they complete
- **AND** no separator or placeholder SHALL appear ahead of the caret before the user reaches that position

#### Scenario: Correcting an earlier segment

- **WHEN** the user moves the caret back into an earlier segment and edits a digit
- **THEN** the edit SHALL apply at that position without the caret being forced to the end of the field

### Requirement: The date field SHALL request a numeric keypad

The field SHALL set `inputMethodHints` to request a digits-first soft keyboard (`Qt.ImhDigitsOnly | Qt.ImhPreferNumbers`), which is honoured more reliably on Android than `Qt.ImhDate`.

#### Scenario: Numeric keyboard on focus

- **WHEN** the user focuses a date field on a touch device
- **THEN** the soft keyboard SHALL present numeric input

### Requirement: The date field SHALL carry an accessible label and spoken format hint

Each date field SHALL have a persistent visible label and an `Accessible.description` (or accessible name) that states the expected entry order in words, derived from the locale (e.g. "enter month, then day, then year"). The format hint SHALL NOT be conveyed by a placeholder alone, since placeholders disappear on input and are inconsistently announced.

#### Scenario: Format hint spoken in locale order

- **WHEN** a screen reader focuses the roast date field under a month-first locale
- **THEN** the announcement SHALL include the order in words as month, then day, then year

#### Scenario: Format hint not placeholder-only

- **WHEN** the field's accessibility information is examined
- **THEN** the expected order SHALL be present in the accessible description, independent of any placeholder text

### Requirement: Stored date value SHALL remain ISO regardless of display order

The value persisted for a bag's roast/frozen/defrost date SHALL be normalized to ISO `yyyy-mm-dd`, independent of the locale display order. On commit, a completed localized entry SHALL be parsed and converted to ISO; an empty field SHALL persist as an empty/null date; an incomplete or unparseable entry SHALL NOT corrupt the stored value.

#### Scenario: Localized entry stored as ISO

- **WHEN** the user completes a valid date under a `DD/MM/YYYY` locale (e.g. `05/03/2026`)
- **THEN** the stored value SHALL be `2026-03-05`

#### Scenario: Blank date allowed

- **WHEN** the user leaves the date field empty and commits
- **THEN** the stored date SHALL be empty/null and no error SHALL be raised

#### Scenario: Incomplete entry does not corrupt stored value

- **WHEN** the user commits an incomplete entry that cannot be parsed to a valid date
- **THEN** the field SHALL NOT write a malformed value as the stored date

### Requirement: Displayed value SHALL stay consistent with the stored value after commit

When entry is committed and the stored value is derived (parsed/normalized) from what the user typed, the field's displayed text SHALL reflect the value that was actually stored, so displayed and stored representations do not diverge.

#### Scenario: Display reflects committed value

- **WHEN** the user commits a date and the stored value is normalized
- **THEN** the field SHALL display a representation consistent with the stored value rather than a stale unparsed string

#### Scenario: Calendar selection updates the field

- **WHEN** the user selects a date from the calendar picker
- **THEN** the field SHALL display the selected date and store its ISO value

### Requirement: Calendar picker buttons SHALL be individually identifiable

Each date field's calendar-open button SHALL have a distinct accessible name identifying which date it sets (e.g. "Open calendar to pick roast date"), so a screen-reader user encountering multiple date rows can tell the buttons apart. The field's accessibility information SHALL make the calendar button discoverable without requiring the user to swipe blindly.

#### Scenario: Distinct calendar button names

- **WHEN** a screen reader navigates the roast, frozen, and defrost calendar buttons
- **THEN** each SHALL announce a name that identifies its specific date

### Requirement: The date picker SHALL provide year navigation

The date picker SHALL offer controls to move to the previous and next year in addition to previous/next month, so reaching a date in an earlier year does not require stepping through twelve months. The year controls SHALL be labeled for assistive technology ("Previous year", "Next year").

#### Scenario: Jump a full year

- **WHEN** the user activates the "Previous year" control in the date picker
- **THEN** the displayed month SHALL move back by one year

#### Scenario: Year controls are labeled

- **WHEN** a screen reader focuses the year navigation controls
- **THEN** they SHALL announce "Previous year" and "Next year" respectively

### Requirement: Date entry scope SHALL be Gregorian with Western digits

The accessible date field SHALL localize only the segment order and separator. It SHALL use the Gregorian calendar and accept Western (ASCII) digits; locale-native digit sets and non-Gregorian calendars are out of scope for this capability.

#### Scenario: Non-Gregorian locale still uses Gregorian entry

- **WHEN** the host locale is associated with a non-Gregorian calendar or non-Western digits
- **THEN** the field SHALL still accept Western digits and interpret the value as a Gregorian date, applying only the locale's segment order and separator
