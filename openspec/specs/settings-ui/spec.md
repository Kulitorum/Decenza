# settings-ui Specification

## Purpose
Defines the organization of the Settings page's tab bar and content: a live-filtering search dialog backed by a static searchable-settings index, the consolidated Calibration/Machine/History & Data/About/Language & Access tabs (merging and renaming previously scattered cards), the fixed tab order, and card-label renames for clarity. Also covers the Visualizer tab's Auto-Update Shots toggle and its dependency on Auto-Upload Shots.

## Requirements

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
The app SHALL provide a "Machine" settings tab (renamed from Preferences) containing: Theme Mode, Auto-Sleep, Auto-wake Schedule, Battery Charging, Shot Review Timer (renamed from "Close Shot Review Screen"), Edit After Shot, Clear Notes on Start, Refill Kit, Screen Zoom (renamed from "Per-Screen Scale"), Simulation Mode (renamed from "Unlock GUI"), Launcher Mode (Android), Shot Map, Water Level Status, and Water Refill Threshold. Cards SHALL be organized in three columns: Power & Schedule (left), App Behavior (middle), Water & Features (right). Shot Review Timer, Edit After Shot and Clear Notes on Start SHALL share one "Shot Review" card in the App Behavior column. The last two control the post-shot review, not uploading, and SHALL keep their existing settings keys so current values carry over. Edit After Shot's description SHALL read "Open shot review page after each extraction".

#### Scenario: User finds Auto-Sleep in Machine tab
- **WHEN** user navigates to Settings > Machine
- **THEN** the Auto-Sleep card is visible in the Power & Schedule column

#### Scenario: Renamed cards show new labels
- **WHEN** user opens the Machine tab
- **THEN** the tab shows "Shot Review Timer", "Screen Zoom", and "Simulation Mode" (not the old names)

#### Scenario: Post-shot review options moved
- **WHEN** user opens the Machine tab
- **THEN** a "Shot Review" card in the App Behavior column holds Shot Review Timer, "Edit After Shot" and "Clear Notes on Start", the last two with the values they had before the move
- **AND** neither appears on the Shot Upload tab

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
The app SHALL order settings tabs as: Connections, Machine, Calibration, History & Data, Themes, Layout, Screensaver, Shot Upload, AI, MQTT, Language & Access, About. Debug tab (debug builds only) SHALL always appear last. The Shot Upload tab replaces the former Visualizer tab in the same position.

#### Scenario: Setup tabs are first
- **WHEN** user opens Settings
- **THEN** the first four tabs are Connections, Machine, Calibration, History & Data

#### Scenario: Debug tab is last
- **WHEN** app is running a debug build
- **THEN** the Debug tab appears after About as the last tab

#### Scenario: Shot Upload replaces Visualizer
- **WHEN** user opens Settings
- **THEN** a "Shot Upload" tab appears between Screensaver and AI, and no tab is labelled "Visualizer"

### Requirement: Setting Card Renames
The app SHALL rename the following setting card labels for clarity: "Per-Screen Scale" → "Screen Zoom", "Close Shot Review Screen" → "Shot Review Timer", "Unlock GUI" → "Simulation Mode", "Ignore Stop-at-Volume with Scale" → "Prefer Weight over Volume", "Stop-at-Weight Calibration" → "Weight Stop Timing". Translation keys SHALL be updated accordingly. The underlying Settings property names and Q_PROPERTY bindings SHALL remain unchanged.

#### Scenario: Screen Zoom replaces Per-Screen Scale
- **WHEN** user navigates to the Machine tab
- **THEN** the card is labeled "Screen Zoom" not "Per-Screen Scale"

#### Scenario: Prefer Weight over Volume replaces Ignore SAV
- **WHEN** user navigates to the Calibration tab
- **THEN** the card is labeled "Prefer Weight over Volume" not "Ignore Stop-at-Volume with Scale"

### Requirement: Auto-Update Shots toggle appears in Visualizer settings tab

The Upload settings card of the Shot Upload settings tab SHALL display an **Auto-Update Shots** toggle positioned immediately below the **Auto-Upload Shots** toggle. The toggle SHALL be disabled (non-interactive, visually dimmed) when **Auto-Upload Shots** is off, reflecting that auto-update depends on the upload feature being active. It applies to every destination that is switched on and connected.

The toggle SHALL bind to `Settings.upload.autoUpdate` (stored under the same key, `visualizer/autoUpdate`, as before) and use translation keys `"settings.visualizer.autoUpdate"` (label, fallback "Auto-Update Shots") and `"settings.upload.autoUpdateDesc"` (description, fallback "Re-send a shot after you edit it" — no longer naming Visualizer, since it applies to both destinations).

#### Scenario: Auto-Update toggle appears below Auto-Upload

- **WHEN** the user navigates to Settings → Shot Upload
- **THEN** an "Auto-Update Shots" toggle SHALL be visible in the Upload settings card directly below "Auto-Upload Shots"

#### Scenario: Auto-Update toggle is disabled when Auto-Upload is off

- **GIVEN** the Auto-Upload Shots toggle is off
- **WHEN** the user views the Shot Upload settings tab
- **THEN** the Auto-Update Shots toggle SHALL appear disabled and SHALL NOT be interactive

#### Scenario: Auto-Update toggle is enabled when Auto-Upload is on

- **GIVEN** the Auto-Upload Shots toggle is on
- **WHEN** the user views the Shot Upload settings tab
- **THEN** the Auto-Update Shots toggle SHALL be interactive and reflect the current `autoUpdate` value

### Requirement: Conditional HDS update action in Connections

Settings → Connections SHALL remain visually unchanged unless a newer eligible HDS firmware release is available for the currently selected connected HDS, on any supported transport. In that case, the selected-scale actions SHALL show an **Update** button immediately beside **Forget**. Activating it SHALL open an accessible confirmation dialog that displays the installed and available versions, the GitHub release notes for the available version, and an explicit action to start the update.

The dialog SHALL describe the update as starting on the scale and SHALL NOT instruct the user to select a release or confirm anything on the scale's display.

#### Scenario: No HDS update is available

- **WHEN** no selected connected HDS has a newer eligible release
- **THEN** Settings → Connections SHALL show no HDS update control, placeholder, banner, or error state

#### Scenario: HDS update is available

- **WHEN** the selected connected HDS has a newer eligible release
- **THEN** an **Update** button SHALL appear immediately beside the selected scale's **Forget** button

#### Scenario: User reviews and confirms the update

- **WHEN** the user activates the HDS **Update** button
- **THEN** an accessible modal dialog SHALL present the installed version, available version, and GitHub release notes
- **AND** the dialog SHALL offer Cancel and Start update actions

#### Scenario: Update has been requested

- **WHEN** the user confirms Start update
- **THEN** the dialog SHALL state that the update was requested and that the scale restarts if it accepts
- **AND** it SHALL NOT claim the scale accepted or installed the update, since two of the three transports carry no acknowledgement
- **AND** it SHALL NOT direct the user to complete anything on the scale's display

#### Scenario: Selected scale changes while dialog is open

- **WHEN** the selected scale changes or the HDS disconnects before Start update is confirmed
- **THEN** the confirmation dialog SHALL close without sending an update command

### Requirement: Shot Upload tab has a switch per destination and one set of upload settings

The settings tab that holds upload settings SHALL be labelled "Shot Upload". It SHALL show the two upload destinations as two cards side by side, **Visualizer** (visualizer.coffee) on the left and **Decent account** (decentespresso.com) on the right, and below them ONE **Upload settings** card that applies to both. Each destination card SHALL use the same order:
1. a header with the destination name and a large on/off switch for uploading to it — Visualizer on by default, Decent off by default;
2. a one-line description and a status line (not connected / connected as `<name>` / sign in again);
3. account controls, identical for both destinations — sign-in fields and a Connect button when not connected, the connected identity and a Disconnect button when connected. Connect checks the credentials with the service before anything is saved, so there is no separate Test Connection: credentials that were not accepted, or could not be checked, are not stored. Connecting switches that destination on; the user can switch it off afterwards;
4. destination-specific actions — Visualizer: sign-up link and Recover Shots; Decent: "View my shots on decentespresso.com" and the most recent upload result.

The Upload settings card SHALL hold "Auto-upload shots", "Auto-update shots" and "Minimum Duration". These are single settings shared by both destinations; no destination card SHALL carry its own copy. They SHALL keep the values the Visualizer tab's settings of the same names had (stored under the same keys), so nothing changes for an existing Visualizer user. A destination receives uploads only while its switch is on and its account is connected. On a narrow screen the cards SHALL stack — Visualizer, Decent account, Upload settings — and every card SHALL scroll fully into view.

#### Scenario: Two destinations, one set of settings
- **WHEN** the user opens Settings → Shot Upload on a tablet
- **THEN** a Visualizer card and a Decent account card are visible side by side, each with its own on/off switch and account controls
- **AND** one Upload settings card below them holds the automatic-upload, automatic-update and minimum-length settings

#### Scenario: Connecting an account
- **WHEN** the user enters a Visualizer username and password and taps Connect, and visualizer.coffee accepts them
- **THEN** the credentials are saved, the card shows "Connected as <username>" with a Disconnect button, and the Visualizer switch is on

#### Scenario: Wrong password
- **WHEN** Visualizer or Decent does not accept the credentials
- **THEN** nothing is saved and the card shows "Email or password not accepted"

#### Scenario: Turning a destination off
- **WHEN** the user switches Visualizer off
- **THEN** no shot is uploaded or updated on Visualizer, automatically or from the review page's Upload button, until it is switched back on
- **AND** the Visualizer account stays connected

#### Scenario: Existing Visualizer user upgrades
- **WHEN** a user who had Auto-upload off and a 10-second minimum on the old Visualizer tab updates the app
- **THEN** the Upload settings card shows Auto-upload off and a 10-second minimum, and the Visualizer switch is on

#### Scenario: Phone width
- **WHEN** the tab is shown at phone width
- **THEN** the Visualizer card, Decent account card and Upload settings card appear in that order and all are reachable by scrolling

#### Scenario: Screen reader order
- **WHEN** a TalkBack or VoiceOver user moves through the tab
- **THEN** focus visits every control of the Visualizer card, then the Decent account card, then the Upload settings card

### Requirement: About tab shows the machine serial number

The About tab's DE1 machine card (the card holding the DE1 firmware status) SHALL show the connected machine's serial number as read from the machine, labelled "Serial number". The value SHALL be selectable or copyable as text, so a user can quote it to Decent support. When no machine is connected, or the machine reported no serial, the line SHALL read "Serial number unknown — connect DE1". In simulation mode it SHALL show the serial the simulator reports — `SIM-DE1`, or the per-run test serial set over MCP.

#### Scenario: Connected machine
- **WHEN** a real DE1 is connected and the user opens Settings → About
- **THEN** the machine card shows "Serial number" followed by the machine's serial

#### Scenario: No machine
- **WHEN** no DE1 is connected
- **THEN** the machine card shows "Serial number unknown — connect DE1"

#### Scenario: Screen reader
- **WHEN** a TalkBack or VoiceOver user reaches the serial line
- **THEN** it is announced as "Serial number" followed by the value
