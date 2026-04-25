## 0. Review
- [x] 0.1 Review proposal decisions and tab structure with stakeholder
- [x] 0.2 Finalize tab ordering and within-tab card ordering
- [x] 0.3 Decide settings search UX (modal dialog, not dropdown)

## 1. Settings Search
- [x] 1.1 Create `SettingsSearchIndex.js` — static JS array with `{ tabIndex, title, description, keywords }` for all settings
- [x] 1.2 Create `SettingsSearchDialog.qml` — modal Dialog with text field, filtered results list using `AccessibleButton` delegates, tab badge per result
- [x] 1.3 Add search icon to tab bar in `SettingsPage.qml` (right end of tab bar)
- [x] 1.4 Implement navigate-to-tab on result tap
- [x] 1.5 Add scroll-to-card + highlight behavior within tab after navigation (requires `objectName` on cards)
- [x] 1.6 Add new files to `CMakeLists.txt`

## 2. Rename Preferences → Machine
- [x] 2.1 Rename `SettingsPreferencesTab.qml` → `SettingsMachineTab.qml`
- [x] 2.2 Update `SettingsPage.qml` tab button text from "Preferences" to "Machine"
- [x] 2.3 Update translation keys (`settings.tab.preferences` → `settings.tab.machine`)
- [x] 2.4 Update `CMakeLists.txt` file reference

## 3. Create Calibration Tab
- [x] 3.1 Create `SettingsCalibrationTab.qml` with 5 cards: Flow Calibration, Weight Stop Timing, Heater Calibration, Virtual Scale, Prefer Weight over Volume
- [x] 3.2 Move card QML from Machine tab to Calibration tab (cut-paste, preserve bindings)
- [x] 3.3 Order cards: Flow Cal → Weight Stop Timing → Heater Cal → Virtual Scale → Prefer Weight over Volume
- [x] 3.4 Add to `CMakeLists.txt` and `SettingsPage.qml` tab bar

## 4. Rename Setting Cards
- [x] 4.1 Rename "Per-Screen Scale" → "Screen Zoom" (card title + translation key)
- [x] 4.2 Rename "Close Shot Review Screen" → "Shot Review Timer" (card title + translation key)
- [x] 4.3 Rename "Unlock GUI" toggle → "Simulation Mode" (toggle label + translation key)
- [x] 4.4 Rename "Ignore Stop-at-Volume with Scale" → "Prefer Weight over Volume" (card title + translation key)
- [x] 4.5 Rename "Stop-at-Weight Calibration" → "Weight Stop Timing" (card title + translation key)
- [x] 4.6 Rename "Offline Mode" card title → "Simulation Mode" (card title + translation key)

## 5. Reorder Machine Tab Cards
- [x] 5.1 Reorganize Machine tab into 3-column layout: Power & Schedule (left), App Behavior (middle), Water & Features (right)
- [x] 5.2 Left column order: Auto-Sleep, Auto-wake Schedule, Battery/Charging
- [x] 5.3 Middle column order: Theme Mode, Extraction View, Shot Review Timer, Screen Zoom, Launcher Mode
- [x] 5.4 Right column order: Water Level Status, Water Refill Threshold, Refill Kit, Shot Map, Headless, Steam Heater, Simulation Mode (last)

## 6. Merge History + Data
- [x] 6.1 Merge History and Data tabs into three-column layout: Shot History | Backup | Server & Data
- [x] 6.2 Remove duplicate `shotServerEnabled` toggle (keep only in Server & Data column)
- [x] 6.3 Move Device Migration to a dialog (`DeviceMigrationDialog.qml`) — "Import from Another Device..." button in Shot History column
- [x] 6.4 Keep Factory Reset at bottom of Server & Data column
- [x] 6.5 Update tab button text and translation keys
- [x] 6.6 Delete old `SettingsShotHistoryTab.qml` and `SettingsDataTab.qml` from repo

## 7. Merge Update + About
- [x] 7.1 Add About content (story, donate button, PayPal QR, credits) to left column of Update tab, below update toggles, separated by divider
- [x] 7.2 Keep release notes in right column at full width — no shrinkage
- [x] 7.3 Rename tab from "Update" to "About"
- [x] 7.4 Update translation keys
- [x] 7.5 Delete `SettingsAboutTab.qml` from repo

## 8. Merge Language + Accessibility
- [x] 8.1 Add Accessibility content (TTS toggle, tick sounds, extraction announcements) to Language tab as third column
- [x] 8.2 Rename tab from "Language" to "Language & Access"
- [x] 8.3 Update translation keys
- [x] 8.4 Delete `SettingsAccessibilityTab.qml` from repo

## 9. Update Tab Bar
- [x] 9.1 Reorder tabs in `SettingsPage.qml`: Connections, Machine, Calibration, History & Data, Themes, Layout, Screensaver, Visualizer, AI, MQTT, Language & Access, About, (Debug)
- [x] 9.2 Update lazy loading indices
- [x] 9.3 Update accessibility tab name announcements array
- [x] 9.4 Update `goToSettings()` tab indices in `main.qml` for deep-links
- [x] 9.5 Update cross-tab reference strings ("Settings → Shot History" → "Settings → History & Data", etc.)

## 10. Update MCP
- [x] 10.1 Rename "preferences" category to "machine" in `settings_get` and `settings_set`
- [x] 10.2 Split calibration settings into new "calibration" category
- [x] 10.3 Update tool descriptions and category lists
- [x] 10.4 Update `docs/CLAUDE_MD/MCP_SERVER.md` with new category names

## 11. Cleanup
- [x] 11.1 Delete old tab files from repo: `SettingsPreferencesTab.qml`, `SettingsShotHistoryTab.qml`, `SettingsDataTab.qml`, `SettingsAboutTab.qml`, `SettingsAccessibilityTab.qml`
- [x] 11.2 Verify all settings bindings still work (every Q_PROPERTY referenced in old tabs is referenced in new tabs)
- [x] 11.3 Verify all dialogs still open correctly from their new locations (TOTP setup, factory reset, restore confirm, heater calibration warning)
- [x] 11.4 Remove any orphaned translation keys
- [x] 11.5 Clean up build directory (`build/mac-test`)

## 12. Testing
- [x] 12.1 Manual test: settings search — type partial matches, verify navigate to correct tab
- [x] 12.2 Manual test: settings search accessibility — TalkBack/VoiceOver navigate results, activate with double-tap
- [x] 12.3 Manual test: Machine tab — all ~12 cards read and write correctly, renamed cards show new labels
- [x] 12.4 Manual test: Calibration tab — all 5 cards read and write correctly, renamed cards show new labels
- [x] 12.5 Manual test: merged History & Data tab — server toggle, security, backup, DE1 import, device migration dialog
- [x] 12.6 Manual test: merged About tab — version info, update controls, release notes scroll, donate button, credits
- [x] 12.7 Manual test: merged Language & Access tab — language selection, accessibility toggles
- [x] 12.8 Manual test: no duplicate server toggle exists anywhere
- [x] 12.9 Accessibility: all moved elements retain Accessible properties; search dialog is keyboard/TalkBack navigable
- [x] 12.10 Manual test: deep-links from other pages (e.g., "Settings → History & Data") navigate to correct tab
- [x] 12.11 Manual test: Device Migration dialog — full flow (search → connect → auth → manifest → import)
