## MODIFIED Requirements

### Requirement: Stale is a display state, never a lock
A recipe whose linked bag is finished SHALL be fully usable: it SHALL appear in all lists and pills, and activation SHALL apply every stored component, including the recipe's own grind data (recipe-owned per recipe-model — never resolved from the bag, finished or otherwise). Surfaces SHALL show a "bag finished" indication (management card text state, dimmed or badged idle pill) rather than an error.

#### Scenario: Stale recipe activates
- **WHEN** the user activates a recipe whose linked bag is finished
- **THEN** profile, dose, yield, temperature, equipment, steam, and hot-water settings apply, the recipe's own grind applies (unaffected by the bag's finished state), and no error is shown

### Requirement: Manual re-point is one tap from where staleness is seen
A stale recipe's management card SHALL offer a direct "bag finished — choose beans" affordance that opens the bag picker for that recipe, and the wizard summary's bag row SHALL reopen the bag step for any recipe. Selecting a bag SHALL update only the recipe's bag link — grind is never touched by a relink (recipe-owned per recipe-model).

#### Scenario: Fix from the card
- **WHEN** the user taps the bag-finished affordance on a stale recipe card and picks an open bag
- **THEN** the recipe links that bag and the stale state clears

#### Scenario: Relink never rewrites grind
- **WHEN** a recipe is relinked to a different bag (automatically or manually)
- **THEN** the recipe's own grind and rpm are unchanged
