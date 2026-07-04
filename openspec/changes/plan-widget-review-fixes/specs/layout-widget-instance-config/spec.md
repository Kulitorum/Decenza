# layout-widget-instance-config Specification (delta)

## ADDED Requirements

### Requirement: Shot plan display option set

The `shotPlan` and `plan` widget types SHALL expose six per-instance display options in both editors (in-app popup and web layout editor), each labeled for exactly the content it controls: Profile & temperature (`shotPlanShowProfile`), Roaster (`shotPlanShowRoaster`), Coffee (`shotPlanShowCoffee`), Grind (`shotPlanShowGrind`), Roast date (`shotPlanShowRoastDate`), and Dose & yield (`shotPlanShowDoseYield`). Defaults SHALL be ON for all except Roast date. Both editors and the C++ option whitelist SHALL accept the same keys so a configuration set in one editor round-trips through the other.

#### Scenario: Toggles do what their labels say

- **WHEN** a user turns the Coffee option off and leaves Roaster on
- **THEN** the widget hides the coffee name and still shows the roaster brand

#### Scenario: Options round-trip between editors

- **WHEN** `shotPlanShowCoffee` is set to false in the web editor
- **THEN** the in-app editor shows the Coffee toggle unchecked and the widget renders without the coffee name

### Requirement: Page-aware plan widget editor labeling

The settings dialog for a `plan` widget instance SHALL be titled as the Plan widget's own settings (not "Shot Plan Settings") and SHALL state that the display options apply to the shot-plan side, because the steam-plan side has no configurable options.

#### Scenario: Editing a plan widget while it shows the steam plan

- **WHEN** a user opens the options of a `plan` widget while steam context is active
- **THEN** the dialog identifies itself as the Plan widget's settings and explains the toggles affect the shot plan display
