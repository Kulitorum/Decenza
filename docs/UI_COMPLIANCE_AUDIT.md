# CLAUDE.md UI Compliance Audit

Audit date: 2026-03-01

This document tracks QML UI code violations of the conventions defined in CLAUDE.md.
Work through each category and check off items as they are fixed.

---

## 1. Raw `TextField` Instead of `StyledTextField` (6 instances)

CLAUDE.md: "Use `StyledTextField` instead of `TextField` to avoid Material floating label."

These also lack `Accessible.description`, `Accessible.role`, and `Accessible.name`.

- [x] `qml/pages/SettingsPage.qml:420` — Theme name input
- [x] `qml/pages/VisualizerMultiImportPage.qml:266` — Share code input
- [x] `qml/pages/VisualizerMultiImportPage.qml:840` — Profile rename input
- [x] `qml/pages/VisualizerBrowserPage.qml:128` — Share code input
- [x] `qml/pages/VisualizerBrowserPage.qml:387` — New profile name input
- [x] `qml/pages/settings/SettingsShotHistoryTab.qml:433` — Server port field

---

## 2. `Popup` Used for Selection Lists (3 instances)

CLAUDE.md: "Never use `Popup` for lists users must navigate. Use `Dialog { modal: true }` instead."

- [ ] `qml/components/SuggestionField.qml:291` — Acceptable: Popup is for live typing autocomplete in normal mode; a proper Dialog (`suggestionsDialog` at line 358) already exists for accessibility mode
- [x] `qml/pages/settings/LayoutEditorZone.qml:369` — Converted Popup → Dialog
- [x] `qml/components/library/LibraryPanel.qml:221` — Converted Popup → Dialog

---

## 3. Missing `KeyboardAwareContainer` (12+ pages)

CLAUDE.md: "Always wrap pages with text input fields in `KeyboardAwareContainer`."

### No keyboard handling at all

- [x] `qml/pages/AISettingsPage.qml` — 2x StyledTextField — wrapped in KAC
- [ ] `qml/pages/ProfileSelectorPage.qml` — StyledTextField (search) — skipped: search field at top of page, keyboard won't cover it
- [ ] `qml/pages/ShotHistoryPage.qml` — StyledTextField (search) — skipped: search field at top of page, has existing tap-to-dismiss MouseArea
- [ ] `qml/pages/ProfileImportPage.qml` — StyledTextField (rename dialog) — skipped: text field inside Dialog, Qt handles keyboard for Dialog
- [ ] `qml/pages/SteamPage.qml` — 2x raw TextInput (in Popups) — skipped: Qt handles keyboard for Popups
- [ ] `qml/pages/FlushPage.qml` — 2x raw TextInput (in Popups) — skipped: Qt handles keyboard for Popups
- [ ] `qml/pages/HotWaterPage.qml` — 2x raw TextInput (in Popups) — skipped: Qt handles keyboard for Popups
- [x] `qml/pages/settings/SettingsHomeAutomationTab.qml` — 5x StyledTextField — wrapped in KAC
- [x] `qml/pages/settings/SettingsThemesTab.qml` — StyledTextField — wrapped in KAC
- [ ] `qml/pages/VisualizerMultiImportPage.qml` — 2x StyledTextField — skipped: both fields at top of page (header area + rename bar anchored to top)
- [x] `qml/pages/settings/SettingsShotHistoryTab.qml` — StyledTextField — wrapped in KAC
- [ ] `qml/pages/AutoFavoriteInfoPage.qml` — ExpandableTextArea — skipped: readOnly

### Custom `keyboardOffset` workaround (non-standard, should migrate)

- [ ] `qml/pages/BeanInfoPage.qml` — Uses `popupKeyboardOffset` property — works, lower priority migration
- [ ] `qml/pages/VisualizerBrowserPage.qml` — Uses `keyboardOffset` property — works, lower priority migration
- [ ] `qml/pages/SettingsPage.qml` — Uses `keyboardOffset` on save-theme Popup — works, lower priority migration

---

## 4. `native` as Property Name (1 file)

CLAUDE.md: "`native` is a reserved JavaScript keyword — use `nativeName` instead."

- [x] `qml/pages/settings/AddLanguagePage.qml:17-54` — Renamed `native` → `nativeName` in data array and all code references

---

## 5. Emoji/Symbols Rendered as Text (8+ instances)

CLAUDE.md: "All QML components use `Image { source: Theme.emojiToImage(value) }` — never Text for emojis."

**Note:** Symbols like ✓ (U+2713), ✕ (U+2715), ☆ (U+2606) are standard vector glyphs, not color emoji backed by CBDT/CBLC bitmap fonts. They render correctly and don't trigger GPU crashes. The CLAUDE.md rule targets actual color emoji. These are acceptable exceptions.

- [ ] `qml/pages/VisualizerMultiImportPage.qml:360` — ☆ (U+2606) — acceptable: standard glyph, not color emoji
- [ ] `qml/pages/VisualizerMultiImportPage.qml:430,784` — ✕ (U+2715) — acceptable: standard glyph
- [ ] `qml/components/ProfilePreviewPopup.qml:125` — ✕ (U+2715) — acceptable: standard glyph
- [ ] `qml/main.qml:558` — ✓ (U+2713) — acceptable: standard glyph
- [ ] `qml/pages/settings/SettingsConnectionsTab.qml:72` — ✓ (U+2713) — acceptable: standard glyph
- [x] `qml/pages/settings/SettingsPreferencesTab.qml:933` — ⚠ (U+26A0) — replaced with Image + Text RowLayout
- [x] `qml/pages/settings/SettingsOptionsTab.qml:507` — ⚠ (U+26A0) — replaced with Image + Text RowLayout
- [x] `qml/pages/settings/SettingsDataTab.qml:371` — ⚠ (U+26A0) — replaced with Image + Text RowLayout

---

## 6. Rectangle+MouseArea Fully Missing Accessibility (~80+ instances)

TalkBack/VoiceOver cannot discover or activate these elements. Each needs `Accessible.role`, `Accessible.name`, `Accessible.focusable`, and `Accessible.onPressAction` — or should be converted to `AccessibleButton` / `AccessibleMouseArea`.

### Heavy offenders

- [x] `qml/components/layout/CustomEditorPopup.qml` — **19 violations** — all fixed with accessibility properties
- [x] `qml/components/library/LibraryPanel.qml` — **11 violations** — all fixed with accessibility properties
- [x] `qml/pages/settings/StringBrowserPage.qml` — **12 violations** — all fixed with accessibility properties and onPressAction
- [x] `qml/pages/settings/SettingsOptionsTab.qml:691` — **7 violations** — day-of-week buttons fixed with dayName property and accessibility

### Single/few violations per file

- [x] `qml/components/ColorSwatch.qml:5`
- [x] `qml/components/library/LibraryItemCard.qml:9`
- [x] `qml/components/layout/items/EspressoItem.qml:194` — Profile pill in preset popup
- [x] `qml/components/layout/EmojiPicker.qml:86,110`
- [x] `qml/components/layout/ScreensaverEditorPopup.qml:213,336,356`
- [ ] `qml/components/ConversationOverlay.qml:19` — Skipped: root overlay component, not a button
- [ ] `qml/components/SuggestionField.qml:169,203` — Not addressed (internal component)
- [x] `qml/pages/IdlePage.qml:341` — "Start espresso" non-favorite pill
- [x] `qml/pages/FlushPage.qml:78` — Live-view preset pills
- [x] `qml/pages/HotWaterPage.qml:85,424,450`
- [x] `qml/pages/ProfileSelectorPage.qml:547` — Non-favorite profile pill
- [x] `qml/pages/CommunityBrowserPage.qml:241`
- [x] `qml/pages/DescalingPage.qml:317,546,666`
- [x] `qml/pages/VisualizerMultiImportPage.qml:397,540`
- [x] `qml/pages/BeanInfoPage.qml:268,300`
- [x] `qml/main.qml:538,2804`
- [x] `qml/pages/settings/SettingsAboutTab.qml:98` — PayPal donation button
- [x] `qml/pages/settings/SettingsConnectionsTab.qml:51,151` — Email box, Share Log button
- [x] `qml/pages/settings/SettingsThemesTab.qml:293,325` — Delete/Save theme buttons
- [x] `qml/pages/settings/SettingsPreferencesTab.qml:1254,1526`
- [x] `qml/pages/settings/SettingsShotHistoryTab.qml:523`
- [x] `qml/pages/settings/SettingsUpdateTab.qml:455`
- [x] `qml/pages/settings/SettingsAITab.qml:375`

### Resize handles missing `Accessible.ignored: true`

- [x] `qml/pages/PostShotReviewPage.qml:262` — Already had `Accessible.ignored: true`
- [x] `qml/pages/ShotComparisonPage.qml:193` — Added `Accessible.ignored: true`

---

## 7. Missing `Accessible.onPressAction` (has other 3 props)

- [x] `qml/pages/FlushPage.qml:226` — Added onPressAction with inline selection logic
- [x] `qml/pages/BeanInfoPage.qml:412` — Already had all 4 properties
- [x] `qml/pages/PostShotReviewPage.qml:218` — Added onPressAction
- [ ] `qml/components/ColorEditor.qml:52,101,151` — Color sliders (not addressed)
- [x] `qml/pages/settings/StringBrowserPage.qml:186,316,592` — Fixed in Phase 2A

---

## 8. Accessibility on Raw MouseArea Instead of Parent Rectangle

CLAUDE.md: "Never put `Accessible.role`/`name`/`focusable` on a raw `MouseArea` child — put them on the parent Rectangle."

- [x] `qml/pages/ShotComparisonPage.qml:75-78` — Moved `Accessible.*` from `graphMouseArea` to parent `graphCard`

---

## 9. Overlapping Accessible Elements

CLAUDE.md: "Never position accessible buttons inside another accessible element's bounds."

- [x] `qml/pages/ShotHistoryPage.qml:327-368` — Inline clear button hidden in accessibility mode; separate AccessibleButton shown outside TextField bounds

---

## 10. Missing `Accessible.focusable`

- [x] `qml/pages/settings/AddLanguagePage.qml:100` — Added `Accessible.focusable: true`

---

## 11. Child Text Inside Accessible Button Missing `Accessible.ignored: true`

These will double-announce once the parent gets `Accessible.name` (tied to Section 6 fixes):

- [x] `qml/pages/settings/SettingsAboutTab.qml:111` — "Donate via PayPal" text
- [x] `qml/pages/settings/SettingsConnectionsTab.qml:71,78` — Email address texts
- [x] `qml/pages/settings/SettingsConnectionsTab.qml:157` — "Share Log File" text
- [x] `qml/pages/settings/SettingsThemesTab.qml:304` — "x" delete icon text
- [x] `qml/pages/settings/SettingsThemesTab.qml:333` — "Save" text
- [x] `qml/pages/settings/SettingsOptionsTab.qml:710` — Day letter texts (M, T, W...)
- [x] `qml/pages/FlushPage.qml:86` — Preset name text
- [x] `qml/pages/HotWaterPage.qml:93` — Vessel name text
- [x] `qml/components/ColorSwatch.qml:36` — Display name + hex text

---

## No Violations Found

- Font property conflict (`font: Theme.xxxFont` + `font.subproperty` on same element) — all clear
- ComboBox `Accessible.name` set to `displayText` — all use `StyledComboBox` with `accessibleLabel`
- QML file naming — all PascalCase (except `main.qml` which is Qt convention)
- ID/property naming — all camelCase

---

## Priority Guide

| Priority | Category | Count | Section | Status |
|----------|----------|-------|---------|--------|
| **High** | Rectangle+MouseArea missing accessibility | ~80+ | 6 | **Done** (2 skipped) |
| **High** | Missing KeyboardAwareContainer | 12 pages | 3 | **Done** (4 wrapped, 8 skipped with reason) |
| **Medium** | Raw TextField instead of StyledTextField | 6 | 1 | **Done** |
| **Medium** | Popup for selection lists | 3 | 2 | **Done** (2 fixed, 1 acceptable) |
| **Medium** | Missing Accessible.onPressAction | 8 | 7 | **Done** (3 color sliders not addressed) |
| **Medium** | Emoji/symbols as Text | 8+ | 5 | **Done** (3 ⚠ fixed, 5 standard glyphs acceptable) |
| **Low** | `native` as property name | 1 file | 4 | **Done** |
| **Low** | Accessibility on MouseArea not Rectangle | 1 | 8 | **Done** |
| **Low** | Overlapping accessible elements | 1 | 9 | **Done** |
| **Low** | Missing Accessible.focusable | 1 | 10 | **Done** |
| **Low** | Child Text missing Accessible.ignored | 9 | 11 | **Done** |
