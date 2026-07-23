## ADDED Requirements

### Requirement: Temperature offset control shows the resulting temperature

On the coffee/espresso details step, the temperature control edits an offset applied to the selected profile. The wizard SHALL display, adjacent to that offset control, the resulting brew temperature the current offset produces — so a user can dial to a target temperature without knowing the profile's default. The readout SHALL render through the same formatter the brew-settings Temp Delta uses (`TemperatureDisplay::format`, via `ProfileManager.temperatureDisplayForSteps`) against the **selected** profile's own frame temperatures — never the app's active profile — so the wizard and brew settings read identically. It SHALL therefore show at most two temperatures (a single value when the profile's frames share one temperature, two distinct values as a spaced list, or first…last for a ramp of three or more), be unit-aware (°C/°F, re-rendering when the unit setting changes), and carry a signed offset tag when the offset is non-zero. The readout SHALL be visible only when the profile's temperature is resolvable, and SHALL be exposed to assistive technology as static text. This requirement applies to the coffee/espresso path only; tea edits an absolute temperature and hot-water tea has no profile anchor.

#### Scenario: Zero offset shows the profile temperature

- **WHEN** the details step is shown for a coffee drink whose profile brews at 94 °C and the temperature offset is 0°
- **THEN** the resulting-temperature readout shows the profile's temperature (e.g. "→ 94°C") with no offset tag

#### Scenario: Adjusted offset shows the resulting temperature and the tag

- **WHEN** the user sets the temperature offset to +2° on a 94 °C profile
- **THEN** the readout shows the resulting temperature with a signed offset tag (e.g. "→ 96°C +2°")

#### Scenario: Multi-temperature profile collapses to two readings

- **WHEN** the selected profile's frames use three or more distinct temperatures and an offset is applied
- **THEN** the readout shows only the shifted first and last temperatures joined by an ellipsis (e.g. "→ 88…94°C"), matching how brew settings renders the same profile

#### Scenario: Fahrenheit unit

- **WHEN** the temperature unit is set to Fahrenheit
- **THEN** the readout shows the resulting temperature and offset in °F, and re-renders when the unit is switched

#### Scenario: Unresolvable profile temperature

- **WHEN** the selected profile's temperature cannot be resolved
- **THEN** the resulting-temperature readout is hidden (the offset control is already disabled in this state)
