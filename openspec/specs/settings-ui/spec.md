# settings-ui Specification

## Purpose
Defines the organization of the Settings page's tab bar and content: a live-filtering search dialog backed by a static searchable-settings index, the consolidated Calibration/Machine/History & Data/About/Language & Access tabs (merging and renaming previously scattered cards), the fixed tab order, and card-label renames for clarity. Also covers the Visualizer tab's Auto-Update Shots toggle and its dependency on Auto-Upload Shots.

## Requirements

### Requirement: Settings Search Dialog

A search icon at the left of the settings tab bar SHALL open a modal dialog whose results filter live on setting titles, descriptions and keywords, best match first. Each result SHALL be an accessible button naming the setting, its card and its tab. Tapping one SHALL close the dialog, open its tab, then scroll to and briefly highlight its control (or card); a result whose control or card is hidden SHALL still open its tab.

#### Scenario: User searches for a setting by name
- **WHEN** user taps the search icon and types "wake"
- **THEN** the results list shows "Auto-Wake" with a "Screensaver" tab badge
- **AND** tapping the result navigates to the Screensaver tab and highlights the Auto-wake card

#### Scenario: User searches by keyword synonym
- **WHEN** user types "power" in the search field
- **THEN** results include "Auto-Sleep" (which has "power" as a keyword) even though "power" is not in the title

#### Scenario: User finds a control inside a card
- **WHEN** user types the name of a single switch, slider or field that sits inside a larger card
- **THEN** that control appears as its own result, labelled with its card and tab
- **AND** tapping it scrolls to and highlights that control's row, not the whole card

#### Scenario: Search dialog is accessible
- **WHEN** a TalkBack/VoiceOver user opens the search dialog
- **THEN** focus is trapped inside the dialog
- **AND** each result has Accessible.role, Accessible.name, and Accessible.focusable
- **AND** double-tap activates the result

### Requirement: Settings Search Index
The app SHALL derive its searchable-settings index from the settings cards themselves: each card and each adjustment on it declares its own search title, optional description and keyword synonyms where it is defined, and the index is generated from those declarations rather than maintained as a separate list. Each result SHALL route to a tab and card (and, for an adjustment, to its control) or to an out-of-settings destination.

#### Scenario: New setting is searchable
- **WHEN** a developer adds a new setting card or a new control on a card to any tab
- **THEN** it is in the search index without a second file being edited

#### Scenario: Renamed or removed card leaves no stale result
- **WHEN** a card or control is renamed or removed
- **THEN** the index no longer offers the old result, because the index is derived from what the tabs declare

#### Scenario: Cards previously missing from search are found
- **WHEN** user types "fahrenheit", "celsius" or "units"
- **THEN** the Temperature unit setting is a result
- **AND** "thermometer" finds Sensor Calibration and "descale" finds Steam Health

### Requirement: Calibration Settings Tab
The app SHALL provide a "Calibration" settings tab containing measurement tuning settings: Flow Calibration, Weight Stop Timing (renamed from "Stop-at-Weight Calibration"), Heater Calibration, Virtual Scale (FlowScale) enable toggle, and Prefer Weight over Volume (renamed from "Ignore Stop-at-Volume with Scale") toggle. Cards SHALL be ordered by adjustment frequency: Flow Cal, Weight Stop Timing, Heater Cal, Virtual Scale, Prefer Weight over Volume.

#### Scenario: User finds all calibration settings in one tab
- **WHEN** user navigates to Settings > Calibration
- **THEN** Flow Calibration, Weight Stop Timing, Heater Calibration, Virtual Scale, and Prefer Weight over Volume are all visible

#### Scenario: Calibration cards ordered by frequency
- **WHEN** user opens the Calibration tab
- **THEN** Flow Calibration appears first and Prefer Weight over Volume appears last

### Requirement: Machine Settings Tab

The app SHALL provide a "Machine" settings tab (renamed from Preferences) with cards in three columns: Power & Schedule (left), App Behavior (middle), and Water & Features (right). It SHALL contain Theme Mode, Auto-Sleep, Auto-wake Schedule, Battery Charging, Refill Kit, Screen Zoom, Simulation Mode, Launcher Mode (Android), Shot Map, Water Level Status and Water Refill Threshold.

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

### Requirement: Shot Review card groups the post-shot options

Shot Review Timer (renamed from "Close Shot Review Screen"), Edit After Shot and Clear Notes on Start SHALL share one "Shot Review" card in the App Behavior column. They control the post-shot review, not uploading, and SHALL keep their existing settings keys. Edit After Shot's description SHALL read "Open shot review page after each extraction".

#### Scenario: Shot Review options keep their stored values

- **WHEN** the user opens the Machine tab after updating
- **THEN** Edit After Shot and Clear Notes on Start SHALL show the values they had before the move

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

The Upload settings card of the Shot Upload settings tab SHALL display an **Auto-Update Shots** toggle directly below the **Auto-Upload Shots** toggle. The toggle SHALL be disabled (non-interactive, visually dimmed) when **Auto-Upload Shots** is off, because auto-update depends on the upload feature being active. It applies to every destination that is switched on and connected.

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

### Requirement: Auto-Update Shots SHALL keep its stored key and shared translation keys

The toggle SHALL bind to `Settings.upload.autoUpdate`, stored under the same key `visualizer/autoUpdate` as before. It SHALL use translation keys `"settings.visualizer.autoUpdate"` (label, fallback "Auto-Update Shots") and `"settings.upload.autoUpdateDesc"` (description, fallback "Re-send a shot after you edit it"). The description SHALL NOT name a single destination.

#### Scenario: Description applies to both destinations

- **WHEN** the user reads the Auto-Update Shots description
- **THEN** it SHALL NOT name Visualizer

### Requirement: Conditional HDS update action in Connections

Settings → Connections SHALL remain visually unchanged unless a newer eligible HDS firmware release is available for the selected, connected HDS on any supported transport. In that case the selected-scale actions SHALL show an **Update** button immediately beside **Forget**. Activating it SHALL open an accessible confirmation dialog showing the installed and available versions, the GitHub release notes for the available version, and Cancel and Start update actions.

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

### Requirement: The HDS update dialog SHALL describe an update that starts on the scale

The dialog SHALL describe the update as starting on the scale. It SHALL NOT direct the user to select a release or confirm anything on the scale's display. After Start update, it SHALL say the update was requested, and SHALL NOT claim the scale accepted or installed it, since two of the three transports carry no acknowledgement.

#### Scenario: Dialog points to no scale display action

- **WHEN** the user reads the HDS update confirmation dialog
- **THEN** it SHALL NOT instruct the user to act on the scale's display

### Requirement: Shot Upload tab has a switch per destination and one set of upload settings

The settings tab that holds upload settings SHALL be labelled "Shot Upload". It SHALL show the two destinations as cards side by side, **Visualizer** (visualizer.coffee) on the left and **Decent account** (decentespresso.com) on the right, with one **Upload settings** card below them that applies to both. On a narrow screen the cards SHALL stack in that order, and every card SHALL scroll fully into view.

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

### Requirement: Destination-specific actions SHALL stay on their own card

Visualizer SHALL offer a sign-up link and Recover Shots. Decent SHALL offer "View my shots on decentespresso.com" and show the most recent upload result.

#### Scenario: Decent card shows its last upload result

- **WHEN** the user views the Decent account card after an upload
- **THEN** the most recent upload result SHALL appear on that card

### Requirement: Upload settings SHALL be single settings shared by both destinations

The Upload settings card SHALL hold "Auto-upload shots", "Auto-update shots" and "Minimum Duration" as single settings shared by both destinations. No destination card SHALL carry its own copy. These SHALL keep the keys and values of the former Visualizer tab settings of the same names.

#### Scenario: Shared setting changes apply to both destinations

- **WHEN** the user changes Minimum Duration in the Upload settings card
- **THEN** the new minimum SHALL apply to uploads for every switched-on destination

### Requirement: Connect SHALL verify credentials before storing them

Connect SHALL check the credentials with the service before anything is saved, so there is no separate Test Connection. Credentials that are not accepted, or could not be checked, SHALL NOT be stored. Connecting SHALL switch that destination on, and the user MAY switch it off afterwards.

#### Scenario: Unreachable service stores nothing

- **WHEN** the user taps Connect and the service cannot be reached
- **THEN** nothing SHALL be saved and the card SHALL show the connection could not be checked

### Requirement: Each destination card SHALL use one fixed layout

Each destination card SHALL use this order: a header with the destination name and a large on/off switch (Visualizer on by default, Decent off by default); a one-line description and status line; account controls identical for both destinations; then destination-specific actions. A destination SHALL receive uploads only while its switch is on and its account is connected.

#### Scenario: Account controls match across destinations

- **WHEN** a destination account is not connected
- **THEN** its card SHALL show sign-in fields and a Connect button, the same as the other destination

### Requirement: About tab shows the machine serial number

The About tab's DE1 machine card SHALL show the connected machine's serial number as selectable text, labelled "Serial number". With no machine connected, or none reported, the line SHALL read "Serial number unknown — connect DE1". In simulation mode it SHALL show the simulator's serial, `SIM-DE1` or the per-run test serial set over MCP.

#### Scenario: Connected machine
- **WHEN** a real DE1 is connected and the user opens Settings → About
- **THEN** the machine card shows "Serial number" followed by the machine's serial

#### Scenario: No machine
- **WHEN** no DE1 is connected
- **THEN** the machine card shows "Serial number unknown — connect DE1"

#### Scenario: Screen reader
- **WHEN** a TalkBack or VoiceOver user reaches the serial line
- **THEN** it is announced as "Serial number" followed by the value

### Requirement: Settings Search Completeness Is Enforced At Build
The desktop build SHALL fail when a settings tab contains a card or an interactive control that would not appear in search. This covers a card not declared through the searchable card form, a control whose search title cannot be read at build time, two results on one card with the same title, and a searchable declaration placed where search navigation cannot reach it. A stale generated index SHALL also fail the build.

#### Scenario: Card added without search info
- **WHEN** a developer adds a card to a settings tab without declaring its search title
- **THEN** the desktop build fails with a diagnostic naming the file and line

#### Scenario: Control with no readable title
- **WHEN** a developer adds a switch whose name is built only at runtime
- **THEN** the desktop build fails until the control is given a title that can be read at build time

#### Scenario: Duplicate titles on one card
- **WHEN** two controls on one card resolve to the same search title
- **THEN** the desktop build fails, because a result could not say which of the two it opens

#### Scenario: Generated index out of date
- **WHEN** the declarations on the tabs no longer match the committed index
- **THEN** the build regenerates the index and fails once, asking for the regenerated file to be committed

### Requirement: Search Matching Is Ranked And Tolerant
Matching SHALL tolerate small typos, SHALL ignore letter case and accents, and SHALL require every word of the query to match. Titles SHALL outrank keywords, and keywords SHALL outrank descriptions. In any language, the English keyword synonyms and the English fallback titles SHALL remain searchable alongside the translated text.

#### Scenario: Typo still finds the setting
- **WHEN** user types "farenheit" or "celcius"
- **THEN** the Temperature unit setting is a result

#### Scenario: Accents ignored
- **WHEN** the app language is French and user types "temperature" without accents
- **THEN** results include settings whose translated title contains "température"

#### Scenario: Title match ranks first
- **WHEN** a query matches one setting's title and another setting's keyword
- **THEN** the title match is listed above the keyword match

#### Scenario: English keyword in another language
- **WHEN** the app language is German and user types "bluetooth"
- **THEN** the machine connection setting is a result

### Requirement: Search Offers Only Available Settings
A setting that exists only on some platforms or builds SHALL declare that condition once. The same declaration SHALL decide both whether the setting is shown on its tab and whether search offers it, so search never offers a result whose card the user cannot see.

#### Scenario: Android-only setting on desktop
- **WHEN** user searches for "launcher" on macOS
- **THEN** Launcher Mode is not offered, because its card exists only on Android

#### Scenario: Simulator compiled out
- **WHEN** the build has no simulator and user searches for "simulation"
- **THEN** Simulation Mode is not offered
