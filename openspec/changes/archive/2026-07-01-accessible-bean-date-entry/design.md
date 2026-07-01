## Context

Bean roast/frozen/defrost dates are entered in `ChangeBeansDialog` via a `StyledTextField` with `inputMask: "9999-99-99"` plus a calendar button that opens `DatePickerDialog` (built on Qt Quick's `MonthGrid`/`DayOfWeekRow`). A blind user ([#1407](https://github.com/Kulitorum/Decenza/issues/1407)) reports the field is unusable with TalkBack.

Research (Qt docs + web accessibility sources) confirmed the root cause is documented, not folklore: `QLineEdit`/`TextInput` `inputMask` pre-fills the control with a blank-character skeleton, and Qt's accessibility interface exposes `displayText()` (the skeleton) to the platform bridge. `placeholderText` also never shows because the field is never empty. Masked inputs are a recognized screen-reader anti-pattern; the accessible alternatives are (a) an unmasked labeled field with progressive formatting, or (b) a three-field Day/Month/Year split.

An interim fix on branch `a11y/bean-date-entry` (PR #1406) dropped the mask only in accessibility mode and normalized on commit. Review found that approach normalizes on commit (introducing a display/stored divergence) and hard-codes ISO/year-first order. This change supersedes it.

The reporter proposed two options; his wording came through machine translation from Arabic. The reliable, translation-robust signals: a typed text field with a numeric keypad, day-first `DD/MM/YYYY`, or a three-field split — and both of his literal phrasings contain a Qt trap (Qt `inputMask` = the skeleton bug; Qt `Tumbler`/`ComboBox` = poor TalkBack support). The maintainer (US, month-first) noted his own `MM/DD/YYYY` preference is cultural, motivating locale-driven order rather than any fixed choice.

## Goals / Non-Goals

**Goals:**
- Make roast/frozen/defrost date entry fully usable with TalkBack/VoiceOver.
- Serve each user their locale's date order and separator automatically, with no new setting.
- Keep the compact single-field + calendar-button UX for sighted users; no extra focus stops.
- Single-source the date-entry accessibility behavior so the three fields cannot diverge.
- Keep stored data format unchanged (ISO `yyyy-mm-dd`).

**Non-Goals:**
- Three-field Day/Month/Year split (considered; see Decisions).
- Non-Gregorian calendars and locale-native digit sets.
- Any user-facing date-format setting.
- Native OS date picker via JNI.
- On-device TalkBack verification is required but is a release-gating step, not part of this design.

## Decisions

### D1: Single unmasked, locale-aware field over three-field split
Use one text field, not three. **Why:** it preserves the current compact layout and one focus stop, matches the maintainer's chosen direction, and — critically — a single field with locale-driven order avoids the ambiguity that normally motivates three fields (the field's own accessible description states the order in words). Three labeled numeric `TextField`s remain the strongest-pedigree alternative (GOV.UK) and are the fallback if on-device testing shows the single field still confuses users; that fallback would be a separate change. Qt `Tumbler`/`ComboBox` are rejected outright for a split because both are poorly supported by TalkBack (QTBUG-78206, QTBUG-96027, Tumbler a11y gaps).

### D2: Drop `inputMask` entirely (not just in accessibility mode)
Remove the mask for all users, not conditionally. **Why:** the mask's only benefit was format guidance for sighted users, which progressive as-you-type formatting plus a visible label provide without the skeleton. Removing it unconditionally means one code path, no `AccessibilityManager.enabled` branch on the mask, and no risk that the mask reappears when the a11y flag toggles mid-session. Alternative (conditional removal, the PR #1406 approach) was rejected as two code paths with the divergence bug.

### D3: Derive order + separator from `Qt.locale().dateFormat(Locale.ShortFormat)`
Parse the short-format pattern for the first positions of `d`, `M`, `y` to get segment order, and extract the non-letter run as the separator. Normalize a 2-digit-year pattern (`yy`) to a 4-digit year for entry. **Why:** it is the OS-provided, user-controlled source of truth; no new setting (aligns with the project's "prefer fewer settings" principle). Alternatives: a hard-coded order (rejected — cultural bias), or an in-app picker (rejected — duplicates an OS control).

### D4: Progressive formatting in a formatter helper, not `inputMask`
Implement as-you-type separator insertion in `DateUtils.js` (pure functions) invoked from `onTextEdited`, appending separators after the caret only. Keep parse (localized → `QDate`/ISO) and format (ISO → localized) helpers alongside. **Why:** pure functions are unit-testable and keep `BeanDateField` declarative; it satisfies the "never inject ahead of the caret" accessibility rule. Alternative (`QValidator`/regex only) does not give the auto-separator UX the reporter asked for.

### D5: Commit path writes ISO and reconciles the displayed value
On commit, parse the localized text → ISO, store ISO, and set the field's displayed text from the stored value so display and storage cannot diverge (the review finding against PR #1406). Empty stays empty/null; unparseable input does not overwrite a good stored value. **Why:** fixes the divergence bug at the source and keeps `Accessible.description: text` truthful.

### D6: Reuse the existing `BeanDateField` component
Keep the `BeanDateField` inline component (already extracted on the branch) as the single home for the mask-free, locale-aware, progressive-format behavior; the three rows stay one-liners. Keep the per-field distinct calendar-button names and the `DatePickerDialog` year-navigation already added. **Why:** single-sourcing the a11y contract is the point; these two picker wins are orthogonal and already validated.

## Risks / Trade-offs

- **Locale order increases formatter complexity (3 orders × separators)** → isolate all order/separator logic in tested `DateUtils.js` pure functions; `BeanDateField` stays thin.
- **Two-way binding: user typing breaks the `text:` binding, so a later value change may not display** → on commit, imperatively set the field text from the stored value (D5) rather than relying solely on the binding; this is the same reconciliation the calendar path needs.
- **`Qt.ImhDigitsOnly` may hide the separator key on some IMEs** → separators are auto-inserted by the formatter, so the user only needs to type digits; the separator key is not required.
- **Sighted users lose the fixed-format mask** → mitigated by locale-correct order (arguably better for them) plus progressive formatting and a visible format label.
- **Ambiguous parse when order is unusual or user types out of order** → parse strictly against the locale order; on failure, do not corrupt the stored value (D5) and leave the entry for correction.
- **Cannot verify TalkBack behavior in CI** → gate merge on an Android beta build tested on-device by the reporter, per project accessibility rules.

## Migration Plan

- No data migration: stored format (`yyyy-mm-dd`) is unchanged; existing bags load and display in the new locale order automatically.
- Supersede the interim PR #1406 approach: this change replaces the conditional mask-removal with the locale-aware progressive field. Land as an update to the same branch/PR or a follow-up, then verify on-device before merge.
- Rollback: revert is safe — reverting restores the previous field with no stored-data change.

## Open Questions

- Confirm with the reporter (in Arabic if helpful) that a single locale-aware field is acceptable, or whether he specifically wants the three-field split — the answer only changes D1, not the rest.
- Should an unparseable-on-commit entry be visually flagged (e.g. brief inline hint), or silently left for correction? Leaning silent to avoid noise; revisit after on-device testing.
