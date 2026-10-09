## MODIFIED Requirements

### Requirement: Settings Search Dialog

The app SHALL provide a search icon on the left end of the settings tab bar that opens a modal Dialog. Results SHALL filter live as the user types, matching setting titles, descriptions and keyword synonyms, and SHALL be ordered best match first. Each result SHALL be an accessible button showing the setting name, its card and its parent tab. Tapping a result SHALL close the dialog, switch to its tab, scroll to the result's control (or card) and briefly highlight it. A result whose card or control is hidden at that moment SHALL still open its tab, highlighting the card when it is visible.

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

## ADDED Requirements

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
