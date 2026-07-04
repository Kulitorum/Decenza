# plan-widgets Specification

## Purpose
TBD - created by archiving change plan-widget-review-fixes. Update Purpose after archive.
## Requirements
### Requirement: Shot plan sentence content follows the display toggles

The Shot Plan text (used by the `shotPlan` widget) SHALL render as a sentence — "Brew {yield} of {beverage}, using {profile} at {temperature}" — when yield, profile, and temperature are all available. When the profile has no target weight (yield absent) but profile and temperature are available, it SHALL render the yield-less sentence "Brew {beverage}, using {profile} at {temperature}" so the beverage word is preserved. Only when profile or temperature is missing SHALL it degrade to a separator-joined fragment list. The beverage word SHALL follow the current profile's `beverage_type`: "Espresso" for espresso (and unset), "tea" for tea types, and "coffee" for any other coffee beverage (filter, pourover, …). A cleaning or descale profile SHALL replace the sentence entirely with a cleaning notice carrying a do-not-load-coffee warning, rendered bold in the error (red) color with the bean/dose tail suppressed. Each display toggle SHALL gate exactly its named content, in both the plain (accessibility) text and the rich (displayed) text:

- **Profile & temperature** (`shotPlanShowProfile`): profile name and temperature.
- **Roaster** (`shotPlanShowRoaster`): roaster brand only.
- **Coffee** (`shotPlanShowCoffee`, default ON): the coffee (bean) name only.
- **Grind** (`shotPlanShowGrind`): the grinder setting, plus the grinder RPM when one is recorded (`Settings.dye.dyeGrinderRpm` > 0).
- **Roast date** (`shotPlanShowRoastDate`, default OFF): the roast date.
- **Dose & yield** (`shotPlanShowDoseYield`): the dose ("{dose} in") and the target output weight.

Enabled optional segments SHALL appear as a separator-joined tail after the sentence. The plain and rich renderings SHALL be produced by one shared builder so their content cannot drift.

#### Scenario: Filter profile says coffee, not espresso

- **WHEN** the current profile's beverage_type is "filter" (or "pourover")
- **THEN** the sentence reads "Brew {yield} of coffee, using {profile} at {temperature}"

#### Scenario: Profile without a target weight keeps the beverage word

- **WHEN** the current profile has no target weight (e.g. a filter or tea profile with no stop-at-weight) but a profile name and temperature are available
- **THEN** the plan reads "Brew {beverage}, using {profile} at {temperature}" (e.g. "Brew tea, using Tea portafilter/black tea at 105 · 40°C"), not a fragment list without the beverage word

#### Scenario: Cleaning profile warns instead of planning

- **WHEN** the current profile's beverage_type is "cleaning" or "descale"
- **THEN** the widget shows a cleaning notice including a warning not to put coffee in the portafilter, with no dose/bean segments

#### Scenario: Coffee toggle controls the coffee name

- **WHEN** Roaster is OFF and Coffee is ON
- **THEN** the plan shows the coffee name and not the roaster brand

#### Scenario: Grind toggle shows grind and RPM

- **WHEN** Grind is ON, the grinder setting is "2.5", and the recorded RPM is 90
- **THEN** the plan's grind segment shows the grind setting and "90 rpm"

#### Scenario: Grind toggle without RPM

- **WHEN** Grind is ON and no RPM is recorded (dyeGrinderRpm = 0)
- **THEN** the grind segment shows only the grinder setting, with no RPM placeholder

#### Scenario: Saved pre-existing configurations keep their meaning

- **WHEN** a widget saved before this change has `shotPlanShowGrind: false`
- **THEN** the grind segment stays hidden and the coffee name is shown (governed by the new Coffee toggle's ON default)

### Requirement: Override indicators

The Shot Plan text SHALL highlight (accent color) only when a deliberate temperature override is active (`hasTemperatureOverride` and the override differs from the profile temperature by more than 0.1°C). When a deliberate yield override is active (`hasBrewYieldOverride` and the target differs from the profile yield by more than 0.1 g), the yield SHALL render as "{profileYield} → {target}g". Natural drift between measured dose and profile dose SHALL NOT trigger either indicator.

#### Scenario: Yield override shows the arrow

- **WHEN** the user dials a yield override of 40.0 g on a profile whose default yield is 36.0 g
- **THEN** the plan shows "36.0 → 40.0g"

#### Scenario: No override, no arrow

- **WHEN** no yield override is set
- **THEN** the plan shows the single target weight with no arrow

### Requirement: Steam plan sentence

The Steam Plan text (the steam-context rendering of the `shotPlan` widget) SHALL summarise the next steam as "Steam {milk} of milk, using the {pitcher} pitcher for {duration}", degrading to a separator-joined list when a piece is missing, and SHALL render nothing when the selected pitcher preset is the disabled ("Off") preset. When the selected preset's name already contains the word "pitcher" (case-insensitive), the sentence SHALL NOT append another "pitcher", so a preset named "Large Pitcher" never renders "Large Pitcher pitcher". The displayed duration SHALL be the selected preset's effective steam time (scaled when weight-timed steaming has a measured/reference milk, else the preset's base duration), resolved by the single shared `SettingsBrew` helper — never the residue another code path wrote to `steamTimeout`.

#### Scenario: Pitcher name containing "Pitcher"

- **WHEN** the selected preset is named "Large Pitcher"
- **THEN** the sentence reads "…using the Large Pitcher for 30s" (no duplicated word)

#### Scenario: Duration reflects the selected preset

- **WHEN** the user selects a pitcher preset without tapping it to start
- **THEN** the displayed duration matches that preset's effective steam time, not a stale value from a previous session

### Requirement: Page-aware steam mode of the Shot Plan widget

The `shotPlan` widget SHALL display the Shot Plan text except in steam context — steam selected on the idle screen, the steam page active, or the machine actively steaming — where it SHALL display the Steam Plan text, unless the instance's Steam plan option (`shotPlanShowSteamPlan`, default ON) is off. Page/mode state SHALL be read from the app's single existing page-state source (the `Theme` singleton), not from a duplicate copy. Outside steam mode the widget SHALL be an activatable control that opens Brew Settings; in steam mode it SHALL be a read-only summary, and its accessibility role and name SHALL match the mode.

#### Scenario: Switches to steam plan

- **WHEN** the user selects steam on the idle screen and the instance's Steam plan option is on
- **THEN** the widget shows the steam plan and is announced as a read-only "Steam plan"

#### Scenario: Steam plan option off

- **WHEN** the instance's Steam plan option is off and the machine is steaming
- **THEN** the widget keeps showing the shot plan

#### Scenario: Shot side opens Brew Settings

- **WHEN** the widget is showing the shot plan and is tapped (or activated via a screen reader)
- **THEN** Brew Settings opens

