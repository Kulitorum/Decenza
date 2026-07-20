## ADDED Requirements

### Requirement: The equipment window SHALL never open empty and SHALL be skipped when only one package exists

During the creation walk the equipment window SHALL preselect the currently
active package rather than opening with nothing chosen. When the inventory holds
exactly one in-inventory package, the wizard SHALL fill it in and skip the
equipment window entirely, advancing straight to the dose/yield/temp/grind
window — there is nothing to ask; the window stays reachable by stepping back.
Edit/clone flows keep the recipe's own package, and a summary-card jump to the
equipment window always shows it. Web recipe creation SHALL likewise link the
active package (the web form has no equipment editor; updates leave the existing
link untouched).

#### Scenario: Single package skips the window

- **GIVEN** exactly one equipment package in inventory
- **WHEN** the creation walk reaches the details step
- **THEN** the wizard SHALL select that package and open directly on the dose/yield/temp/grind window

#### Scenario: Multiple packages preselect the active one

- **GIVEN** several packages with one active
- **WHEN** the equipment window opens in the creation walk with no package chosen yet
- **THEN** the active package SHALL be preselected, and the window SHALL still be shown

#### Scenario: Web recipe creation links the active package

- **WHEN** a recipe is created from the `/recipes` web form
- **THEN** it SHALL be linked to the active equipment package
- **AND** its grind candidates SHALL resolve against that package's grinder
