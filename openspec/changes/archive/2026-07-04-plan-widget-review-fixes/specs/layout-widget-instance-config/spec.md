# layout-widget-instance-config Specification (delta)

## ADDED Requirements

### Requirement: Shot plan display option set

The `shotPlan` widget type SHALL expose seven per-instance display options in both editors (in-app popup and web layout editor), each labeled for exactly the content it controls: Profile & temperature (`shotPlanShowProfile`), Roaster (`shotPlanShowRoaster`), Coffee (`shotPlanShowCoffee`), Grind (`shotPlanShowGrind`), Roast date (`shotPlanShowRoastDate`), Dose & yield (`shotPlanShowDoseYield`), and Steam plan (`shotPlanShowSteamPlan`). Defaults SHALL be ON for all except Roast date. Both editors and the C++ configurable-type gate SHALL accept the same keys so a configuration set in one editor round-trips through the other. The formerly separate `plan` and `steamPlan` widget types SHALL NOT exist as palette entries or configurable types.

#### Scenario: Toggles do what their labels say

- **WHEN** a user turns the Coffee option off and leaves Roaster on
- **THEN** the widget hides the coffee name and still shows the roaster brand

#### Scenario: Steam plan option gates the page-aware swap

- **WHEN** `shotPlanShowSteamPlan` is off
- **THEN** the widget shows the shot plan even in steam context

#### Scenario: Options round-trip between editors

- **WHEN** `shotPlanShowCoffee` is set to false in the web editor
- **THEN** the in-app editor shows the Coffee toggle unchecked and the widget renders without the coffee name

#### Scenario: Removed widget types stay unregistered

- **WHEN** a saved layout contains an item of type `plan` or `steamPlan`
- **THEN** the item renders nothing and the editors offer no such palette entry or options
