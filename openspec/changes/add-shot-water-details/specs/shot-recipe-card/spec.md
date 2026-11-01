# Spec Delta

## ADDED Requirements

### Requirement: Shot review shows the added-water result
Shot Review and Shot Detail SHALL show an Added water result near the shot results when a shot has a captured water result or an enabled saved water specification. It SHALL show dispensed amount, measurement source and Water temp (set) from the shot's saved data. This result SHALL remain readable when the linked recipe cannot resolve and SHALL be identical in native and Wasm clients.

#### Scenario: Completed Americano
- **WHEN** a saved Americano contains 103.2 g of scale-measured added water and an 85 °C water setting
- **THEN** its Added water result shows 103.2 g and Water temp (set) 85 °C
- **AND** it identifies the amount as scale-measured

#### Scenario: Flow-estimated long black
- **WHEN** a long black contains 120 mL of flow-derived water and a configured temperature of 90 °C
- **THEN** its Added water result shows 120 mL identified as estimated and Water temp (set) 90 °C

#### Scenario: Legacy shot has no measured amount
- **WHEN** a shot has only a saved vessel specification with a 100 g amount and an 85 °C temperature
- **THEN** Added water shows amount not recorded and Water temp (set) 85 °C
- **AND** it does not present 100 g as water dispensed

#### Scenario: Water was not dispensed
- **WHEN** the water result says the step never started
- **THEN** Added water states Not dispensed without implying a completed pour or a measured temperature

#### Scenario: Interrupted water pour
- **WHEN** the water result is interrupted and carries a partial measurement
- **THEN** Added water labels the result as interrupted and the amount as partial

#### Scenario: Recipe no longer resolves
- **WHEN** a shot retains its water result but its linked recipe is unavailable after import
- **THEN** Added water still shows the saved result without consulting current recipe or vessel values

#### Scenario: Plain espresso
- **WHEN** a shot has neither a water result nor an enabled saved water specification
- **THEN** no Added water result is shown

### Requirement: Recipe water intent is distinct from the drink result
The recipe card SHALL NOT present its saved vessel amount as the amount dispensed. The Added water presentation SHALL own the numerical result and set-temperature display; recipe context SHALL retain vessel identity without duplicating those values. All labels and spoken descriptions SHALL be translated and identify estimated, unavailable and partial results.

#### Scenario: Target differs from actual
- **WHEN** the shot's vessel specification says 100 g and its result says 103.2 g
- **THEN** the numerical water result reads 103.2 g once
- **AND** the recipe's vessel name remains available without another unqualified 100 g water amount

#### Scenario: Screen-reader review
- **WHEN** a screen reader focuses Added water
- **THEN** it announces the amount and its source or unavailable status, the set temperature when available, and any interruption

#### Scenario: Fahrenheit preference
- **WHEN** a shot saved at a water setting of 85 °C is opened with Fahrenheit selected
- **THEN** Water temp (set) displays 185 °F while the saved value remains 85 °C
