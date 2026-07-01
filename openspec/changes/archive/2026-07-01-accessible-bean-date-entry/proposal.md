## Why

A blind user ([#1407](https://github.com/Kulitorum/Decenza/issues/1407), TalkBack) reports the bean roast date is effectively impossible to enter. The date fields use a Qt `inputMask` (`"9999-99-99"`), which pre-fills the field with a `____-__-__` skeleton that Qt exposes via `displayText()` to the accessibility tree — so TalkBack announces a skeleton of blanks, not an editable date, and `placeholderText` never shows. This is documented Qt behaviour, corroborated by web accessibility guidance that masked inputs are hostile to screen readers. The date is a required-quality input for freshness tracking, so an inaccessible field blocks blind users from a core workflow.

## What Changes

- Replace the masked roast/frozen/defrost date fields with a **locale-aware, screen-reader-friendly single text field**:
  - Drop `inputMask` entirely (not just in accessibility mode); no pre-filled skeleton ever reaches the accessibility tree.
  - Derive field **order and separator from the host locale** (`Qt.locale().dateFormat(Locale.ShortFormat)`) — US users get `MM/DD/YYYY`, most of the world `DD/MM/YYYY`, ISO locales `YYYY-MM-DD`. No date-format setting is added; the OS locale (which the user already controls) is the source of truth.
  - **Progressive formatting as the user types** (insert the locale separator after the cursor as digits arrive) rather than a pre-filled mask; the field starts genuinely empty.
  - Numeric keypad via `Qt.ImhDigitsOnly | Qt.ImhPreferNumbers` (more reliably honoured on Android than `Qt.ImhDate`).
  - Persistent visible label plus an `Accessible.description` that names the expected order in words (e.g. "enter month, then day, then year") derived from the locale.
  - Gregorian calendar, Western digits; roast dates continue to be **stored internally as ISO `yyyy-mm-dd`** — only the displayed input order/separator is localized.
- Preserve and formalize the two picker improvements already made on this branch:
  - Distinct, per-field calendar-button accessible names ("Open calendar to pick roast/frozen/defrost date").
  - Previous/Next **year** navigation in the date picker so reaching a past date does not require twelve month taps.
- Consolidate the three date rows behind one reusable `BeanDateField` component so the accessibility contract is single-sourced.

Non-goals (explicitly out of scope): a three-field Day/Month/Year split (evaluated as an alternative — see design.md), non-Gregorian calendars, locale-native digit sets, and any user-facing date-format setting.

## Capabilities

### New Capabilities
- `accessible-date-entry`: How the user enters a calendar date (roast/frozen/defrost) via a screen-reader-friendly, locale-aware text field, and the accessibility/navigation requirements of the supporting date picker.

### Modified Capabilities
<!-- None. `change-beans-dialog` specifies roast-date optionality and no-pre-fill; this change does not alter those requirements, only the entry mechanism (new capability). -->

## Impact

- **QML:** `qml/components/ChangeBeansDialog.qml` (three date rows → `BeanDateField` component), `qml/components/DatePickerDialog.qml` (year navigation), `qml/components/DateUtils.js` (locale order/separator parsing + progressive-format + parse/normalize helpers).
- **Behaviour:** input order becomes locale-dependent for display; stored value format is unchanged (`yyyy-mm-dd`). Sighted users lose the fixed-format mask but gain locale-correct order and as-you-type formatting.
- **Reuse:** other date pickers that use `DatePickerDialog` (e.g. `BagCard` thaw date) inherit the year-navigation improvement.
- **Verification:** pure QML, no platform/JNI code, but the screen-reader behaviour is only verifiable on-device with TalkBack; a desktop build confirms compilation only.
- **Supersedes:** the interim mask-removal-in-accessibility-mode approach on branch `a11y/bean-date-entry` (PR #1406), which this change replaces with the locale-aware progressive-format field.
