## ADDED Requirements

### Requirement: Session-Only Brew Overrides
The system SHALL provide session-only (non-persisted) overrides for brew parameters: temperature, dose weight, yield weight, and grind setting. These overrides SHALL NOT modify the saved profile and SHALL be cleared after each shot ends or when the user switches profiles.

#### Scenario: User sets temperature override
- **WHEN** the user sets a brew temperature different from the profile default in the BrewDialog
- **THEN** the system stores a session-only temperature override
- **AND** the IdlePage displays the override as an arrow (e.g., "88 → 90°C")
- **AND** the DE1 machine uses the overridden temperature for the next shot

#### Scenario: User sets dose and yield overrides
- **WHEN** the user configures dose and yield in the BrewDialog and confirms
- **THEN** the system activates brew-by-ratio mode with the specified dose and calculated ratio
- **AND** the shot plan line on IdlePage shows the configured dose and yield

#### Scenario: Overrides persist after a shot
- **WHEN** a shot ends (espresso cycle completes)

#### Scenario: Profile loaded from profile presets
- **WHEN** the user loads a profile from the profile presets list
- **THEN** the temperature override is cleared
- **AND** the dose is reset to the DYE bean weight (default 18g)
- **AND** the yield is reset based on the profile's target weight
- **AND** the ratio is recalculated as yield / dose

#### Scenario: Profile loaded from shot history with overrides
- **WHEN** the user loads a shot from history that has recorded brew overrides
- **THEN** the dose is set from the history override
- **AND** the yield is set from the history override
- **AND** the ratio is recalculated from the loaded dose and yield

#### Scenario: Profile loaded from shot history without overrides
- **WHEN** the user loads a shot from history that has no recorded brew overrides
- **THEN** the yield is set from the profile's target weight
- **AND** the dose is set from the DYE bean weight (default 18g)
- **AND** the ratio is recalculated as yield / dose

#### Scenario: Grind and dose overrides cleared on bean switch
- **WHEN** the user loads a different bean preset
- **THEN** the grind setting and dose overrides are cleared
- **AND** the yield override is cleared
- **AND** the grind setting is the one configured from the bean
- **AND** the dose is the default 18g
- **AND** the yield and ratio are recalculated from the profile's target weight

### Requirement: Brew Dialog
The system SHALL provide a BrewDialog accessible from the shot plan line on IdlePage and from the StatusBar. The dialog SHALL display the current profile name and bean info as a "Base Recipe" header, and allow editing temperature, dose, ratio, yield and grind (in this order) for the next shot.

#### Scenario: Opening the BrewDialog
- **WHEN** the user taps the shot plan text on IdlePage
- **THEN** the BrewDialog opens with current values populated from Settings (DYE metadata and profile defaults)
- **AND** the targetWeight and targetTemperature are set with a precedence order: overrides first, then profile defaults

#### Scenario: Temperature override with save option
- **WHEN** the user changes the temperature in the BrewDialog
- **THEN** the dialog shows the profile default temperature below the input
- **AND** a "Save" button allows permanently updating the profile temperature

#### Scenario: Dose from scale
- **WHEN** the user taps "Get from scale" in the BrewDialog
- **AND** the scale reports weight ≥ 3g
- **THEN** the dose value is updated to the scale reading
- **WHEN** the scale reports weight < 3g
- **THEN** a warning is shown asking the user to place the portafilter on the scale

#### Scenario: Ratio and yield auto-calculation
- **WHEN** the user changes dose or ratio
- **THEN** yield is recalculated automatically (dose × ratio)
- **WHEN** the user manually edits the yield value
- **THEN** the ratio is changed automatically (yield / dose)

#### Scenario: Clear all overrides
- **WHEN** the user taps the "Clear" button in the BrewDialog
- **THEN** all fields reset to profile defaults (temperature) and empty/default values (dose=18g, grind=bean"", ratio=calculated from profiel target weight / 18g)

### Requirement: Shot Plan Display
The system SHALL display a summary line showing the configured shot parameters: profile name with temperature, bean name with grind setting, and dose/yield weights. The line SHALL be clickable to open the BrewDialog. Visibility SHALL be controlled by a "Show shot plan" setting (default: enabled). When a "Show on all screens" setting is enabled, the shot plan line SHALL appear in the top status bar on all pages; otherwise it SHALL appear only on the IdlePage.

#### Scenario: Shot plan with no overrides
- **WHEN** no overrides are active and DYE metadata is populated
- **THEN** the shot plan shows: "ProfileName (88°C) · BeanName (grind) · 18.0g in, 36.0g out"

#### Scenario: Shot plan with temperature override
- **WHEN** a temperature override is active
- **THEN** the temperature portion shows the arrow notation: "ProfileName (88 → 90°C)"

#### Scenario: Shot plan hidden when empty
- **WHEN** no profile is loaded and no DYE metadata is set
- **THEN** the shot plan line is not visible

#### Scenario: Shot plan disabled via settings
- **WHEN** the "Show shot plan" setting is disabled
- **THEN** the shot plan line is not visible on any page

#### Scenario: Shot plan on idle page only (default)
- **WHEN** "Show shot plan" is enabled and "Show on all screens" is disabled
- **THEN** the shot plan line appears only on the IdlePage within the page content

#### Scenario: Shot plan on all screens
- **WHEN** "Show shot plan" is enabled and "Show on all screens" is enabled
- **THEN** the shot plan line appears in the top status bar between the page title and the indicators
- **AND** tapping it opens the BrewDialog from any page

### Requirement: Brew Overrides History Recording
The system SHALL record the active brew overrides (temperature, dose, yield, grind) as a JSON string in the shot history when a shot is saved. This enables traceability of per-shot adjustments.

#### Scenario: Overrides saved to shot history
- **WHEN** a shot ends with active brew overrides
- **THEN** the overrides are serialized to JSON and stored in the `brew_overrides_json` column
- **AND** the overrides JSON is available when viewing the shot in history

#### Scenario: No overrides recorded when none active
- **WHEN** a shot ends without any active brew overrides
- **THEN** the `brew_overrides_json` field is empty/null

### Requirement: Shot History Parameter Retrieval
The system SHALL populate brew parameters (dose, yield, grind) from shot history when a shot is loaded via `loadShotWithMetadata()`. This allows the user to repeat a previous shot's settings. If the shot has recorded brew overrides, those take precedence; otherwise the profile's target weight is used for yield.

#### Scenario: Loading shot with brew overrides from history
- **WHEN** the user loads a shot from history that has `brew_overrides_json` populated
- **THEN** the dose override is set from the recorded override value
- **AND** the yield override is set from the recorded override value
- **AND** the grinder setting is populated from the recorded override value
- **AND** the BrewDialog shows these as active overrides

#### Scenario: Loading shot without brew overrides from history
- **WHEN** the user loads a shot from history that has no `brew_overrides_json`
- **THEN** no dose or yield overrides are set
- **AND** the yield defaults to the loaded profile's target weight
- **AND** the dose defaults to the DYE bean weight (default 18g)

#### Scenario: BrewDialog pre-populated from history
- **WHEN** the BrewDialog opens after loading a shot from history
- **THEN** dose, yield, and grind fields reflect the active overrides (if set) or profile defaults
- **AND** the ratio is calculated from the effective dose and yield
