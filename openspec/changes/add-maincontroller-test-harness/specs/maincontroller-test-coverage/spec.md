## ADDED Requirements

### Requirement: MainController is constructible in isolation for tests

`MainController` SHALL provide test seams that let a unit test construct it without touching the real application database or requiring live hardware, while leaving the production construction path unchanged by default.

#### Scenario: Storages isolated from the real app DB

- **WHEN** a test constructs `MainController` under `QStandardPaths` test mode with a temporary data directory
- **THEN** the internally-created `RecipeStorage`, `ShotHistoryStorage`, and `CoffeeBagStorage` operate against a throwaway DB under the temp dir and never read or write the user's real app database

#### Scenario: Optional storage injection defaults to production behavior

- **WHEN** `MainController` is constructed without any injected storages (the production call)
- **THEN** it creates its storages internally exactly as before the change, with no behavioral difference

#### Scenario: Injected storages are used when provided

- **WHEN** a test constructs `MainController` passing temp-DB-backed storages
- **THEN** `MainController` uses the injected instances instead of creating its own

### Requirement: Active-recipe grind edits refresh the live dial

The harness SHALL assert that an in-place edit of the active recipe's grind reaches `Settings.dye.dyeGrinderSetting` through the real async `recipeUpdated → requestRecipe → recipeReady` chain, and that non-edit reads do not.

#### Scenario: Editing the active recipe's grind pushes to the dial

- **WHEN** the active recipe's `grindPinned` is updated via `requestUpdateRecipe`
- **THEN** `Settings.dye.dyeGrinderSetting` eventually equals the new grind (awaited with `QTRY_VERIFY`)

#### Scenario: Editing an inactive recipe does not touch the dial

- **WHEN** a recipe that is not the active recipe is updated
- **THEN** `Settings.dye.dyeGrinderSetting` is unchanged

#### Scenario: Startup restore does not re-apply grind

- **WHEN** the active recipe is restored on startup (`requestRecipe` without an edit)
- **THEN** the live dial is not overwritten by the recipe's stored grind

#### Scenario: Relink refresh and editor-prefill reads do not push

- **WHEN** the active recipe is re-read due to a bag relink or a QML editor-prefill (not an edit)
- **THEN** `Settings.dye.dyeGrinderSetting` is unchanged

#### Scenario: Grind-less drink type and empty grind leave the dial untouched

- **WHEN** the active recipe is a grind-less drink type (tea) or its `grindPinned` is empty and it is edited
- **THEN** the live dial is not modified

#### Scenario: Zeroing rpmPinned does not clear the dial RPM (recorded limitation)

- **WHEN** the active recipe is edited to set `rpmPinned` to 0
- **THEN** `Settings.dye.dyeGrinderRpm` retains its prior value (documented parity with `applyActivatedRecipe`; the test pins current behavior so a future fix is a deliberate change)

### Requirement: Recipe-wiring invariants are regression-covered

The harness SHALL cover the surrounding recipe-wiring invariants so a regression in the shared machinery is caught.

#### Scenario: The refresh flag is cleared on recipe switch and deactivate

- **WHEN** an edit sets `m_refreshDialFromRecipeEdit` and the user then switches or deactivates the recipe before the re-read lands
- **THEN** the flag does not leak a stale grind onto the newly-active recipe's first read

#### Scenario: The dial→recipe stamp echo-guard does not loop

- **WHEN** an active-recipe edit refreshes the dial, which re-emits `dyeGrinderSettingChanged`
- **THEN** the resulting stamp hits the equality guard and issues no further recipe write (no infinite loop, no `m_pendingRecipeSelfWrites` drift)

#### Scenario: Deactivate-on-ingredient-swap fires

- **WHEN** the live profile, bag, or equipment is changed to something other than the active recipe's own ingredient
- **THEN** the active recipe deactivates

### Requirement: Proxy tests are migrated into the harness

Existing tests whose assertions only stand in for `MainController` signal-wiring behavior SHALL be re-expressed against the real harness and the proxies removed.

#### Scenario: No proxy remains for covered wiring

- **WHEN** the audit identifies a recipe test that uses `RecipeStorage` statics or a pure helper as a substitute for live `MainController` wiring already covered by the harness
- **THEN** its assertions are moved into `tst_maincontroller` and the proxy test is deleted, with no net loss of coverage
