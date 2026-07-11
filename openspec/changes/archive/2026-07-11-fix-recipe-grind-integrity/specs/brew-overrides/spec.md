## MODIFIED Requirements

### Requirement: Persistent Override Storage
The system SHALL store temperature and yield overrides in QSettings for persistence across app sessions. "Active" is tracked by a genuine boolean flag (`hasTemperatureOverride`, `hasBrewYieldOverride`) that reflects whether a deliberate override is currently in effect — not merely whether an override value has ever been set during the session. Overrides SHALL be cleared — the flag set false, not just the value resynced to a new default — when switching profiles, when a recipe is activated (before its own overrides apply), or when the user taps "Clear" in the BrewDialog. Loading a shot or favorite that carries its own frozen override value SHALL only mark the flag active when that frozen value genuinely differs from the freshly-loaded profile's own default (the same threshold the Shot Plan display uses), so a frozen value that happens to already match the current profile never falsely reports as an active override.

#### Scenario: Overrides persist between app sessions
- **WHEN** the user sets temperature or yield overrides in the BrewDialog
- **THEN** the values are immediately saved to QSettings
- **AND** when the app is restarted, the overrides are restored from QSettings
- **AND** the overrides remain active until explicitly cleared

#### Scenario: Overrides cleared on profile switch
- **WHEN** the user switches to a different profile
- **THEN** all overrides are cleared from QSettings, and `hasTemperatureOverride`/`hasBrewYieldOverride` become false
- **AND** the IdlePage shot plan returns to profile defaults with no highlight

#### Scenario: Overrides cleared via BrewDialog
- **WHEN** the user taps "Clear" in the BrewDialog
- **THEN** all overrides are removed from QSettings and the override flags become false
- **AND** the Settings properties are reset to default values

#### Scenario: Overrides cleared on recipe activation
- **WHEN** a recipe is activated
- **THEN** the override flags reflect only that recipe's own stored overrides (or false, if it has none) — not a leftover flag from whatever was active before

#### Scenario: Deactivation alone leaves the live setup untouched
- **WHEN** the active recipe is deactivated without switching profile or bag
- **THEN** the live brew values — including any override the recipe applied — remain in effect (deactivation drops the recipe association, not the dialed setup); the next profile or recipe switch clears them as above

#### Scenario: A frozen shot value matching the current profile is not flagged
- **WHEN** a shot or favorite is loaded whose saved `temperatureOverride` happens to equal the freshly-loaded profile's own default temperature
- **THEN** `hasTemperatureOverride` is false and the Shot Plan shows no highlight
