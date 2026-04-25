## ADDED Requirements

### Requirement: Settings Search Dialog
The app SHALL provide a search function accessible via a search icon on the right end of the settings tab bar. Tapping the icon SHALL open a modal Dialog containing a text field and a scrollable list of matching settings. Results SHALL filter live as the user types, matching against setting titles, descriptions, and keyword synonyms. Each result SHALL be an AccessibleButton showing the setting name and parent tab name. Tapping a result SHALL close the dialog, switch to the correct tab, scroll to the target card, and briefly highlight it.

#### Scenario: User searches for a setting by name
- **WHEN** user taps the search icon and types "wake"
- **THEN** the results list shows "Auto-wake Schedule" with a "Machine" tab badge
- **AND** tapping the result navigates to the Machine tab and highlights the Auto-wake card

#### Scenario: User searches by keyword synonym
- **WHEN** user types "power" in the search field
- **THEN** results include "Auto-Sleep" (which has "power" as a keyword) even though "power" is not in the title

#### Scenario: Search dialog is accessible
- **WHEN** a TalkBack/VoiceOver user opens the search dialog
- **THEN** focus is trapped inside the dialog
- **AND** each result has Accessible.role, Accessible.name, and Accessible.focusable
- **AND** double-tap activates the result

### Requirement: Settings Search Index
The app SHALL maintain a static JS array of searchable settings entries, each containing tabIndex, cardId, title, description, and keywords. The index SHALL cover all user-configurable settings across all tabs.

#### Scenario: New setting is searchable
- **WHEN** a developer adds a new setting card to any tab
- **THEN** they add a corresponding entry to the search index with title, description, and keyword synonyms

### Requirement: Calibration Settings Tab
The app SHALL provide a "Calibration" settings tab containing measurement tuning settings: Flow Calibration, Weight Stop Timing (renamed from "Stop-at-Weight Calibration"), Heater Calibration, Virtual Scale (FlowScale) enable toggle, and Prefer Weight over Volume (renamed from "Ignore Stop-at-Volume with Scale") toggle. Cards SHALL be ordered by adjustment frequency: Flow Cal, Weight Stop Timing, Heater Cal, Virtual Scale, Prefer Weight over Volume.

#### Scenario: User finds all calibration settings in one tab
- **WHEN** user navigates to Settings > Calibration
- **THEN** Flow Calibration, Weight Stop Timing, Heater Calibration, Virtual Scale, and Prefer Weight over Volume are all visible

#### Scenario: Calibration cards ordered by frequency
- **WHEN** user opens the Calibration tab
- **THEN** Flow Calibration appears first and Prefer Weight over Volume appears last

### Requirement: Machine Settings Tab
The app SHALL provide a "Machine" settings tab (renamed from Preferences) containing: Theme Mode, Auto-Sleep, Auto-wake Schedule, Battery Charging, Shot Review Timer (renamed from "Close Shot Review Screen"), Refill Kit, Screen Zoom (renamed from "Per-Screen Scale"), Simulation Mode (renamed from "Unlock GUI"), Launcher Mode (Android), Shot Map, Water Level Status, and Water Refill Threshold. Cards SHALL be organized in three columns: Power & Schedule (left), App Behavior (middle), Water & Features (right).

#### Scenario: User finds Auto-Sleep in Machine tab
- **WHEN** user navigates to Settings > Machine
- **THEN** the Auto-Sleep card is visible in the Power & Schedule column

#### Scenario: Renamed cards show new labels
- **WHEN** user opens the Machine tab
- **THEN** cards display "Shot Review Timer", "Screen Zoom", and "Simulation Mode" (not the old names)

### Requirement: Merged History and Data Tab
The app SHALL provide a single "History & Data" tab combining shot history access, DE1 import, backup/restore, server enable with security, device migration, and factory reset. The server enable toggle SHALL appear exactly once. The tab SHALL use a three-column layout: Shot History (left), Backup (middle), Server & Data (right). Device Migration SHALL be accessible via a button that opens a stepped dialog.

#### Scenario: No duplicate server toggle
- **WHEN** user searches for the server enable toggle
- **THEN** it exists in exactly one location (History & Data tab, Server & Data column)

#### Scenario: Device Migration as dialog
- **WHEN** user taps "Import from Another Device"
- **THEN** a modal dialog opens with the search → auth → manifest → import workflow

#### Scenario: Factory Reset at bottom
- **WHEN** user looks for Factory Reset
- **THEN** it is at the bottom of the Server & Data column, separated by a divider

### Requirement: Merged Update and About Tab
The app SHALL provide a single "About" tab containing version info and update controls in the left column, and release notes in the right column at full width. About content (credits, donation) SHALL appear in the left column below update controls, separated by a divider. Release notes SHALL NOT be reduced in size.

#### Scenario: Release notes at full width
- **WHEN** user navigates to Settings > About
- **THEN** release notes occupy the full right column with the same scrollable height as before

#### Scenario: Credits visible alongside updates
- **WHEN** user navigates to Settings > About
- **THEN** both update controls and credits/donate are visible in the left column without switching tabs

### Requirement: Merged Language and Accessibility Tab
The app SHALL provide a single "Language & Access" tab combining language selection and translation tools with accessibility settings (TTS, tick sounds, extraction announcements). Language selection SHALL be the primary section.

#### Scenario: User finds accessibility settings
- **WHEN** user navigates to Settings > Language & Access
- **THEN** TTS toggle, tick sounds, and extraction announcement settings are visible

#### Scenario: User finds language selection
- **WHEN** user navigates to Settings > Language & Access
- **THEN** the language picker and translation status are visible as the primary section

### Requirement: Settings Tab Order
The app SHALL order settings tabs as: Connections, Machine, Calibration, History & Data, Themes, Layout, Screensaver, Visualizer, AI, MQTT, Language & Access, About. Debug tab (debug builds only) SHALL always appear last.

#### Scenario: Setup tabs are first
- **WHEN** user opens Settings
- **THEN** the first four tabs are Connections, Machine, Calibration, History & Data

#### Scenario: Debug tab is last
- **WHEN** app is running a debug build
- **THEN** the Debug tab appears after About as the last tab

### Requirement: Setting Card Renames
The app SHALL rename the following setting card labels for clarity: "Per-Screen Scale" → "Screen Zoom", "Close Shot Review Screen" → "Shot Review Timer", "Unlock GUI" → "Simulation Mode", "Ignore Stop-at-Volume with Scale" → "Prefer Weight over Volume", "Stop-at-Weight Calibration" → "Weight Stop Timing". Translation keys SHALL be updated accordingly. The underlying Settings property names and Q_PROPERTY bindings SHALL remain unchanged.

#### Scenario: Screen Zoom replaces Per-Screen Scale
- **WHEN** user navigates to the Machine tab
- **THEN** the card is labeled "Screen Zoom" not "Per-Screen Scale"

#### Scenario: Prefer Weight over Volume replaces Ignore SAV
- **WHEN** user navigates to the Calibration tab
- **THEN** the card is labeled "Prefer Weight over Volume" not "Ignore Stop-at-Volume with Scale"

## REMOVED Requirements

### Requirement: Preferences Tab
**Reason**: Renamed to "Machine" with calibration cards extracted to the new Calibration tab. All settings move to their new homes; no settings are deleted.
**Migration**: Tab file renamed from `SettingsPreferencesTab.qml` to `SettingsMachineTab.qml`. Calibration cards (Flow Cal, SAW Cal, Heater Cal, Virtual Scale, Ignore SAV) move to `SettingsCalibrationTab.qml`.

### Requirement: Separate About Tab
**Reason**: About content (credits, donate) is merged into the Update tab to reduce tab count. Tab renamed to "About."
**Migration**: About content appended below update controls in left column.

### Requirement: Separate History and Data Tabs
**Reason**: These tabs had overlapping server controls. Merging eliminates the duplicate `shotServerEnabled` toggle and groups all data management in one place.
**Migration**: History left column + Data columns combined into a single three-column tab.

### Requirement: Separate Accessibility Tab
**Reason**: Accessibility settings are set-once and rarely revisited. Merging with Language (also set-once) reduces tab count without degrading either experience.
**Migration**: Accessibility content added to Language tab. Tab renamed to "Language & Access."
