# UI Compliance Audit — Manual Test Checklist

For branch `ui-compliance-audit-fixes`. All testing is manual (no QML unit tests).

---

## Prerequisites

- Build in Qt Creator (no errors)
- Test on Android device with TalkBack available
- For keyboard tests: use a device with soft keyboard (Android/iOS)

---

## 1. TextField → StyledTextField (6 files)

Verify each field looks correct (no Material floating label, has placeholder text, themed styling).

- [ ] **SettingsPage.qml** — Settings → Themes → Save Theme dialog → theme name input shows placeholder
- [ ] **VisualizerMultiImportPage.qml** — Import Shared Profiles → "Add by Code" → 4-char code field shows "CODE" placeholder, auto-uppercases
- [ ] **VisualizerMultiImportPage.qml** — Import a "D" profile → rename bar appears with "Profile name" placeholder
- [ ] **VisualizerBrowserPage.qml** — Visualizer browser → share code field shows "CODE" placeholder, auto-uppercases
- [ ] **VisualizerBrowserPage.qml** — Visualizer browser → import duplicate → "Save as New" → name field shows "Profile name" placeholder
- [ ] **SettingsShotHistoryTab.qml** — Settings → Shot History → Port field shows "8888" placeholder, only accepts digits

---

## 2. Popup → Dialog (2 files)

Verify the selection lists open, display correctly, and can be dismissed.

- [ ] **LayoutEditorZone.qml** — Settings → Layout → tap a zone → "Change Widget" → widget type picker opens as centered Dialog, can scroll and select, closes on selection
- [ ] **LibraryPanel.qml** — Library → "+" add button → save action menu opens as centered Dialog, options work, closes on selection or outside tap

**TalkBack**: Swipe through the Dialog — focus should be trapped inside (can't reach elements behind it).

---

## 3. KeyboardAwareContainer (4 files)

On Android/iOS, tap the text field and verify content shifts up so the field stays visible above the keyboard.

- [ ] **SettingsHomeAutomationTab.qml** — Settings → Home Automation → tap any of the 5 fields (host, port, username, password, topic) → content shifts above keyboard
- [ ] **AISettingsPage.qml** — AI Settings → tap API key or Ollama endpoint field → content shifts above keyboard
- [ ] **SettingsThemesTab.qml** — Settings → Themes → tap hex color field → content shifts above keyboard
- [ ] **SettingsShotHistoryTab.qml** — Settings → Shot History → tap port field → content shifts above keyboard

---

## 4. Emoji ⚠ → Image (3 files)

Verify the warning icon renders as an SVG image (not text), aligned inline with the message text.

- [ ] **SettingsPreferencesTab.qml** — Settings → Preferences → scroll to warning message → ⚠ icon is an image, aligned with text
- [ ] **SettingsOptionsTab.qml** — Settings → Options → trigger water-low warning → ⚠ icon is an image
- [ ] **SettingsDataTab.qml** — Settings → Data → if storage permission warning visible → ⚠ icon is an image

---

## 5. Reserved keyword rename (1 file)

- [ ] **AddLanguagePage.qml** — Settings → Language → Add Language → grid of languages renders, each shows native name, tapping selects the language

---

## 6. Overlapping accessible elements (1 file)

- [ ] **ShotHistoryPage.qml** — Shot History → type in search → clear button appears and works
- [ ] **ShotHistoryPage.qml (TalkBack)** — With TalkBack on, swipe to find both the search field AND a separate "Clear search" button (not overlapping)

---

## 7. Accessibility — Heavy offender files

### CustomEditorPopup.qml (19 fixes)

Open Settings → Layout → tap a zone → Edit widget.

- [ ] Color picker buttons respond to tap
- [ ] Select text → open color picker → choose color → verify color applied to originally selected text
- [ ] Bold/Italic toggle buttons respond to tap
- [ ] Font size buttons (in Repeater) respond to tap
- [ ] Text alignment buttons respond to tap
- [ ] Action selector buttons respond to tap
- [ ] Tap outside text input dismisses keyboard but does NOT close popup
- [ ] Tapping inside text input to reposition cursor does NOT dismiss keyboard
- [ ] Cancel and Save buttons work
- [ ] **TalkBack**: Swipe through all controls — each is announced with a name, double-tap activates

### LibraryPanel.qml (11 fixes)

Open Library panel.

- [ ] Grid/List display mode toggles work
- [ ] Tab items (if multiple) switch correctly
- [ ] Add/Apply/Delete/Share action buttons work
- [ ] Type filter buttons (Items/Zones/Layouts/Themes) work
- [ ] **TalkBack**: All buttons announced, double-tap activates, no double-announcements

### StringBrowserPage.qml (12 fixes)

Open Settings → Language → Edit strings.

- [ ] Clear AI button works
- [ ] AI Translate button works
- [ ] Filter buttons work
- [ ] Cancel buttons in popups work
- [ ] **TalkBack**: All buttons announced, double-tap activates

### SettingsOptionsTab.qml (7 fixes)

Open Settings → Options → scroll to day-of-week selector.

- [ ] Each day button (M, T, W, T, F, S, S) toggles on tap
- [ ] **TalkBack**: Each button announced as full day name (e.g., "Monday"), not just the letter

---

## 8. Accessibility — Single/few violation files

For each, verify the element is tappable and works. With TalkBack, verify it's announced and double-tap activates.

### Components
- [ ] **ColorSwatch.qml** — Any color swatch (e.g., in theme editor) — tap selects
- [ ] **LibraryItemCard.qml** — Library → tap a card — selects/opens it
- [ ] **EspressoItem.qml** — Layout with espresso widget → non-favorite profile pill — tap works
- [ ] **EmojiPicker.qml** — Layout editor → emoji picker → category tabs switch, clear button works
- [ ] **ScreensaverEditorPopup.qml** — Screensaver editor → map texture buttons, Cancel, Save all work

### Pages
- [ ] **IdlePage.qml** — Idle page → non-favorite profile pill — tap opens profile selector
- [ ] **FlushPage.qml** — Flush page → live preset pills — tap selects preset
- [ ] **HotWaterPage.qml** — Hot water → live vessel presets, weight/volume mode toggles — tap works
- [ ] **ProfileSelectorPage.qml** — Profile selector → non-favorite pill — tap works
- [ ] **CommunityBrowserPage.qml** — Community browser → download button — tap downloads
- [ ] **DescalingPage.qml** — Descaling → Done, Steam toggle, Start Descaling buttons — tap works
- [ ] **VisualizerMultiImportPage.qml** — Import page → star button toggles, details panel deselect works
- [ ] **BeanInfoPage.qml** — Bean info → graph card tap works, resize handle is NOT announced by TalkBack
- [ ] **main.qml** — Layout edit mode → "Done Editing" button works; hide keyboard button works
- [ ] **PostShotReviewPage.qml** — Post-shot review → tap graph card ("Tap to inspect") works
- [ ] **ShotComparisonPage.qml** — Shot comparison → accessibility on graph card (not on MouseArea), resize handle NOT announced

### Settings tabs
- [ ] **SettingsAboutTab.qml** — About → PayPal donation button — tap works
- [ ] **SettingsConnectionsTab.qml** — Connections → email box tap copies, Share Log button works
- [ ] **SettingsThemesTab.qml** — Themes → delete (x) and save buttons work
- [ ] **SettingsPreferencesTab.qml** — Preferences → day-of-week buttons toggle, scroll-down indicator works
- [ ] **SettingsShotHistoryTab.qml** — Shot History → Copy URL button works (when server running)
- [ ] **SettingsUpdateTab.qml** — Updates → scroll-down indicator works
- [ ] **SettingsAITab.qml** — AI tab → conversation overlay dismiss area works

---

## 9. TalkBack regression sweep

With TalkBack enabled, do a quick swipe-through on these key pages to catch any regressions:

- [ ] Idle page
- [ ] Settings (each tab)
- [ ] Profile selector
- [ ] Shot history
- [ ] Library panel

**Check for:**
- No double-announcements (button name + child text)
- All interactive elements discoverable by swiping
- Double-tap activates the correct action
- No "unlabeled button" announcements
