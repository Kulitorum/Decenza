# shot-recipe-card Specification

## Purpose

Covers the recipe card on shot pages: when it appears for a shot that used a recipe, how recipe identity is resolved live while the dial-in stays the shot's own snapshot, which fields each card shows, and its accessibility.

## Requirements

### Requirement: Shot pages show a recipe card when the shot used a recipe
The Shot Detail and Shot Review pages SHALL display a recipe card when, and only when, the opened shot references a recipe (`recipeId > 0`). The card SHALL show the recipe's identity — name, drink type, and profile — so the user can see which recipe the shot was pulled with.

#### Scenario: Shot pulled with a recipe
- **WHEN** the user opens a shot whose record has a recipe id greater than zero
- **THEN** a recipe card appears showing the recipe's name, drink type, and profile

#### Scenario: Shot pulled without a recipe
- **WHEN** the user opens a shot with no recipe id (id absent or zero or negative)
- **THEN** no recipe card is shown and the remaining cards reflow to fill the space

### Requirement: Recipe identity is live-resolved, dial-in is the shot snapshot
The recipe card's identity fields (name, drink type, profile) SHALL be resolved live from the recipe row by `recipeId`; this is safe because a recipe with linked shots can only be archived, never deleted, so the row always resolves. Any grind/rpm the recipe card displays SHALL come from the shot's own snapshot fields, NOT the recipe's current (possibly since-edited) pinned grind.

#### Scenario: Recipe renamed after the shot
- **WHEN** the recipe used by a shot has since been renamed
- **THEN** the recipe card shows the current recipe name

#### Scenario: Recipe pin edited after the shot
- **WHEN** the recipe's pinned grind has been changed since the shot was pulled
- **THEN** the recipe card shows the grind the shot was actually pulled with (the shot snapshot), not the recipe's new pin

#### Scenario: Recipe archived after the shot
- **WHEN** the recipe used by a shot has been archived
- **THEN** the recipe card still resolves and shows the recipe's identity

### Requirement: Each card shows only the data it keeps
On the Shot Detail and Shot Review pages, each card SHALL show only the data that belongs to it. The Equipment card SHALL show grinder, burrs, basket, and puck prep, and SHALL NOT show grind or RPM. Grind/RPM — a per-shot dial-in — SHALL render on the recipe card when the shot used a recipe (`recipeId > 0`), and otherwise on the bean card.

#### Scenario: Equipment card omits grind
- **WHEN** the user opens any shot and views the Equipment card
- **THEN** the card shows grinder identity, burrs, basket, and puck prep, with no grind or RPM line

#### Scenario: Grind on the recipe card
- **WHEN** the opened shot used a recipe and has a recorded grind
- **THEN** the grind/RPM is shown on the recipe card and not repeated on the bean or Equipment cards

#### Scenario: Grind on the bean card
- **WHEN** the opened shot did not use a recipe and has a recorded grind
- **THEN** the grind/RPM is shown on the bean card and not on the Equipment card, and the bean-card title shows no parenthetical grind suffix

### Requirement: The recipe card is accessible
The recipe card SHALL expose a grouping accessible name summarizing the recipe, with decorative inner items ignored, and all its user-visible text internationalized.

#### Scenario: Screen reader reads the recipe card
- **WHEN** a screen reader focuses the recipe card
- **THEN** it announces the recipe's identity as a single grouped item

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
