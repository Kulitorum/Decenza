# shot-recipe-card Specification

## Purpose
TBD - created by archiving change shot-pages-card-cleanup. Update Purpose after archive.
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

