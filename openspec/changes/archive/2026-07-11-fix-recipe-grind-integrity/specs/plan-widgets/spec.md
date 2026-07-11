## MODIFIED Requirements

### Requirement: Override indicators

The Shot Plan text SHALL highlight (accent color) only when a deliberate temperature override is active (`hasTemperatureOverride` and the override differs from the profile temperature by more than 0.1°C). When a deliberate yield override is active (`hasBrewYieldOverride` and the target differs from the profile yield by more than 0.1 g), the yield SHALL render as "{profileYield} → {target}g". Natural drift between measured dose and profile dose SHALL NOT trigger either indicator. Because `hasTemperatureOverride`/`hasBrewYieldOverride` are genuinely cleared on profile switch and recipe activation/deactivation (brew-overrides), the highlight SHALL NOT persist or flicker on based on stale state left over from a previous profile or recipe.

#### Scenario: Yield override shows the arrow

- **WHEN** the user dials a yield override of 40.0 g on a profile whose default yield is 36.0 g
- **THEN** the plan shows "36.0 → 40.0g"

#### Scenario: No override, no arrow

- **WHEN** no yield override is set
- **THEN** the plan shows the single target weight with no arrow

#### Scenario: Browsing recipes without changing temperature leaves no stale highlight

- **WHEN** the user switches between several recipes that all use their profile's own default temperature (no deliberate override on any of them)
- **THEN** the Shot Plan never highlights while browsing, regardless of what was highlighted before the browsing started
