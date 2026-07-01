## 1. Locale-aware date helpers (`qml/components/DateUtils.js`)

- [x] 1.1 Add `dateOrderFromFormat(fmt)` returning the segment order (`["y","M","d"]` permutation) parsed from a locale short-format string, falling back to `["y","M","d"]` — pure function; the QML caller passes `Qt.locale().dateFormat(Locale.ShortFormat)`
- [x] 1.2 Add `dateSeparatorFromFormat(fmt)` returning the separator run (default `-`)
- [x] 1.3 Add `orderWords(order)` returning the order in words (e.g. "month, then day, then year") for the accessible description
- [x] 1.4 Add `formatAsTyped(text, order, separator)` inserting separators progressively after complete segments, starting from an empty string
- [x] 1.5 Add `localizedToIso(text, order)` parsing a completed localized entry to `yyyy-mm-dd`; returns "" for blank/incomplete/unparseable (never corrupts stored value)
- [x] 1.6 Add `isoToLocalized(iso, order, separator)` for displaying a stored ISO date in locale order
- [x] 1.7 `normalizeDateString` left unchanged; existing callers unaffected

## 2. Rework `BeanDateField` (`qml/components/ChangeBeansDialog.qml`)

- [x] 2.1 Removed `inputMask` from the field entirely (no accessibility-mode branch)
- [x] 2.2 Set `inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhPreferNumbers`
- [x] 2.3 Display driven from stored ISO `value` via `isoToLocalized(...)` (Component.onCompleted + Connections on value change when not focused)
- [x] 2.4 `onTextEdited` applies `formatAsTyped(...)` (progressive formatting)
- [x] 2.5 `onEditingFinished` parses via `localizedToIso(...)`, emits `valueEdited(iso)`, and sets field text from the committed value so display and storage cannot diverge; leaves a good stored value untouched on unparseable input
- [x] 2.6 Persistent label + `Accessible.description` including `orderWords()`; format hint not placeholder-only
- [x] 2.7 Empty entry commits as empty/null (roast/defrost optional; frozen behavior preserved via freeze switch)
- [x] 2.8 Three call sites still pass distinct `fieldAccessibleName`/`calendarAccessibleName`; `textField` alias still wires `KeyboardAwareContainer.textFields` and `onOpened` focus

## 3. Date picker (`qml/components/DatePickerDialog.qml`)

- [x] 3.1 Previous/Next year controls present with accessible names "Previous year"/"Next year"
- [x] 3.2 Calendar returns ISO `yyyy-mm-dd` (and "" on clear) via `dateSelected` → `valueEdited`, so display reconciles per 2.5

## 4. Validation

- [ ] 4.1 Unit coverage for `DateUtils.js` helpers — BLOCKED: project has no QML/JS test harness (all tests are C++ `tst_*.cpp`). Needs a QuickTest target stood up, or the pure logic exercised another way. See wrap-up.
- [x] 4.2 `qmllint` (resolved import paths) on both QML files — no errors, no new warnings (count 217, down from 227 baseline)
- [ ] 4.3 Quick compile / QML-load check via Qt Creator (macOS) — qmllint passed statically; runtime QML load not yet verified
- [ ] 4.4 Manual matrix: US (month-first), a day-first locale, ISO locale — entry, progressive formatting, calendar selection, stored value (needs running app)

## 5. On-device accessibility verification (release gate)

- [ ] 5.1 Trigger an Android beta/test build for the branch
- [ ] 5.2 With TalkBack on: empty field announces label (no skeleton), spoken order matches locale, numeric keypad appears, progressive formatting usable, calendar buttons distinct, year navigation works
- [ ] 5.3 Confirm with the reporter (issue #1407) before merge; capture feedback

## 6. Wrap-up

- [x] 6.1 Commit the locale-aware implementation to the branch / PR #1406, superseding the interim mask-removal approach
- [ ] 6.2 Archive this OpenSpec change (`/opsx:archive`) as the final commit on the branch before merge
- [ ] 6.3 Decide on `DateUtils.js` test coverage (4.1): stand up QuickTest, or accept manual/on-device verification for this change
