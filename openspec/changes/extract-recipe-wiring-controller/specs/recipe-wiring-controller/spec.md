## ADDED Requirements

### Requirement: Recipe wiring lives in a dependency-injected controller

The recipe/dial/bag write-through logic SHALL live in a `RecipeWiringController` that depends only on injected port interfaces (dial, recipe-store, profile, side-effects) and has no direct Qt-event-loop, database, or `Settings` dependency in its core.

#### Scenario: Controller core is constructible without Qt/DB

- **WHEN** the controller is constructed in a unit test with fake ports
- **THEN** it operates entirely through those ports — no `RecipeStorage`, `SettingsDye`, `ProfileManager`, event loop, or database is required

#### Scenario: MainController adapts the controller

- **WHEN** the app runs
- **THEN** `MainController` forwards the recipe/dial Qt signals to the controller's methods and implements the ports over the real `RecipeStorage`/`SettingsDye`/`ProfileManager`, and the `activeRecipe` property delegates to the controller's cache

### Requirement: Extraction is behavior-preserving

The extraction SHALL NOT change any user-observable recipe behavior. Activation, stamping, dial refresh, and deactivation SHALL behave identically to before the move.

#### Scenario: No functional change

- **WHEN** the app activates, edits, and deactivates recipes as before
- **THEN** the observable outcomes (dial values, active-recipe id, deactivation on ingredient swap, heater hold) are unchanged, verified by the unit tests plus a manual pass

### Requirement: The wiring contract is unit-tested with fakes

The controller SHALL be covered by `tst_recipewiringcontroller` driving fake ports synchronously, asserting the full wiring contract.

#### Scenario: Editing the active recipe's grind refreshes the dial

- **WHEN** the active recipe's grind is updated and the store delivers the edit re-read
- **THEN** the fake dial's grinder setting equals the new grind

#### Scenario: Editing an inactive recipe does not touch the dial

- **WHEN** a non-active recipe is updated
- **THEN** the fake dial's grinder setting is unchanged

#### Scenario: Non-edit reads do not refresh the dial

- **WHEN** the active recipe is re-read for startup restore, a relink refresh, or an editor prefill (not an edit)
- **THEN** the fake dial is not written

#### Scenario: Grind-less drink type and empty grind leave the dial untouched

- **WHEN** the active recipe is a grind-less type (tea) or its grind is empty and it is edited
- **THEN** the fake dial is not written

#### Scenario: The edit-refresh flag is cleared on switch/deactivate

- **WHEN** an edit arms the refresh but the recipe is switched or deactivated before the re-read arrives
- **THEN** the pending re-read does not push a stale grind onto the next active recipe

#### Scenario: The dial→recipe stamp echo-guard does not loop

- **WHEN** a dial refresh re-enters as a dial-changed event
- **THEN** the controller's equality guard issues no further store update (no loop, no self-write-count drift)

#### Scenario: Deactivate-on-ingredient-swap fires

- **WHEN** the profile, bag, or equipment changes to a value other than the active recipe's own ingredient
- **THEN** the controller deactivates the active recipe

#### Scenario: Zeroing rpm does not clear the dial rpm (recorded limitation)

- **WHEN** the active recipe is edited to set rpm to 0
- **THEN** the fake dial's rpm retains its prior value (documented parity with activation; the test pins current behavior so a future fix is deliberate)

### Requirement: Extraction establishes the strangler template

This change SHALL be structured so the same ports/controller/fakes pattern can be reused for future `MainController` extractions.

#### Scenario: Pattern is reusable

- **WHEN** a later change needs to test another `MainController` slice (e.g. shot lifecycle)
- **THEN** it can follow the same approach — extract a controller behind ports and unit-test it with fakes — without a full-graph harness
