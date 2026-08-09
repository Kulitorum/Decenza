## ADDED Requirements

### Requirement: The pitcher picker offers "Heater off"

The wizard's steam pitcher picker SHALL offer the built-in "Heater off" entry alongside the real pitcher presets, so a recipe can be saved as wanting the steam heater off. It SHALL NOT filter heater-off entries out of the picker, and it SHALL NOT offer any way to create one.

Choosing it SHALL store the off marker on the recipe's steam block rather than a pitcher name. The steam and summary cards SHALL show it as the chosen entry, distinguishable from a real pitcher rather than shown with an empty duration and temperature.

#### Scenario: Heater off is selectable
- **WHEN** the user opens the pitcher picker while composing or editing a recipe
- **THEN** the built-in "Heater off" entry is offered and can be selected

#### Scenario: Choosing it stores the marker
- **WHEN** the user selects "Heater off" and saves the recipe
- **THEN** the recipe's steam block carries the off marker and no pitcher name

#### Scenario: The wizard cannot create one
- **WHEN** the user adds a new pitcher preset from the wizard or the Steam page
- **THEN** no heater-off option is offered

#### Scenario: A recipe carrying the marker displays it
- **WHEN** a recipe carrying the off marker is opened in the wizard
- **THEN** the steam card names the "Heater off" entry as the selection, not a blank pitcher
