# recipe-bag-lifecycle Delta

## ADDED Requirements

### Requirement: Roll-on-finish relinks recipes to the successor bag
When a bag is marked finished and a newer open bag of the same bean exists (matched by Bean Base canonical id when available, otherwise case-insensitive roaster+coffee identity), the system SHALL automatically relink the finished bag's recipes to the newest open bag of that bean — except any recipe whose relink would duplicate an existing recipe on the target bag (same profile title and same drink type). Skipped recipes SHALL remain linked to the finished bag. When no newer bag of the bean exists, all of the finished bag's recipes SHALL remain linked to it. The relink SHALL be silent (no dialog, no confirmation, no setting) and SHALL be announced with a toast naming how many recipes moved.

#### Scenario: Single successor bag
- **WHEN** a bag with two linked recipes is marked finished and one newer open bag of the same bean exists
- **THEN** both recipes relink to the newer bag and a toast reports the move

#### Scenario: Dup-guard protects a comparison pair
- **WHEN** two recipes with the same profile and drink type link two different bags of the same bean, and one bag is finished
- **THEN** the finished bag's recipe is NOT relinked (it would duplicate the survivor) and shows the bag-finished state

#### Scenario: Different drinks are not duplicates
- **WHEN** a finished bag's latte recipe would roll onto a bag that already has an espresso recipe with the same profile
- **THEN** the latte relinks — same profile with a different drink type is not a duplicate

#### Scenario: No successor
- **WHEN** a bag is finished and no other open bag of that bean exists
- **THEN** its recipes keep the finished bag link and show the bag-finished state

### Requirement: Wake-on-restock relinks stale recipes to the new bag
When a new bag is added — or an existing bag returns to inventory — the system SHALL automatically relink stale recipes (recipes whose linked bag is no longer in inventory) matching that bag's bean identity, applying the same dup-guard. When multiple stale twins (same profile and drink type) match, they SHALL wake one at a time in most-recently-used order — the first relinks, the rest stay stale. The relink SHALL be silent and announced with a toast.

#### Scenario: Restock wakes a sleeping recipe
- **WHEN** the user adds a new bag of a bean whose only recipe is stale
- **THEN** the recipe relinks to the new bag and a toast reports it

#### Scenario: Stale twins wake one at a time
- **WHEN** two stale recipes with the same profile and drink type match a newly added bag
- **THEN** only the most recently used one relinks; the other remains stale

### Requirement: Stale is a display state, never a lock
A recipe whose linked bag is finished SHALL be fully usable: it SHALL appear in all lists and pills, and activation SHALL apply every stored component including the finished bag's grind data. Surfaces SHALL show a "bag finished" indication (management card text state, dimmed or badged idle pill) rather than an error.

#### Scenario: Stale recipe activates
- **WHEN** the user activates a recipe whose linked bag is finished
- **THEN** profile, dose, yield, temperature, equipment, steam, and hot-water settings apply, grind resolves from the finished bag, and no error is shown

### Requirement: Manual re-point is one tap from where staleness is seen
A stale recipe's management card SHALL offer a direct "bag finished — choose beans" affordance that opens the bag picker for that recipe, and the wizard summary's bag row SHALL reopen the bag step for any recipe. Selecting a bag SHALL update only the recipe's bag link (grind inherit/pin state is preserved; pinned grind is untouched).

#### Scenario: Fix from the card
- **WHEN** the user taps the bag-finished affordance on a stale recipe card and picks an open bag
- **THEN** the recipe links that bag and the stale state clears

#### Scenario: Relink never rewrites grind
- **WHEN** a recipe with a pinned grind is relinked (automatically or manually)
- **THEN** the pin is unchanged; an inheriting recipe simply follows the new bag's dial
