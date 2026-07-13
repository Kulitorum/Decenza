## ADDED Requirements

### Requirement: MainController is constructible in isolation for tests

The test suite SHALL construct a real `MainController` against an isolated throwaway database with no live hardware and no changes to production code.

#### Scenario: Storages isolated from the real app DB

- **WHEN** a test constructs `MainController` under `QStandardPaths` test mode with a clean temporary data directory
- **THEN** the internally-created `ShotHistoryStorage`, `RecipeStorage`, `CoffeeBagStorage`, and `EquipmentStorage` operate against a throwaway `shots.db` under the temp dir and never read or write the user's real app database

#### Scenario: Construction is inert headless

- **WHEN** `MainController` is constructed in the test with default settings
- **THEN** no listening socket is bound (ShotServer disabled by default), no broker/network connection is initiated on construction, and no production source is modified to make this possible

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

### Requirement: Coverage is net-new, with no proxy tests to migrate

The audit of existing recipe tests SHALL be recorded, and any genuine proxy for `MainController` wiring migrated. The audit performed for this change found **none**: every existing recipe test (`tst_recipeselectionmodel`, `tst_recipestorage`, `tst_recipegenerator`, `tst_recipeparams`, `tst_recipepromotion`) is a genuine `RecipeStorage`/`RecipeSelectionModel`/generator unit test, not a stand-in for the live signal wiring — so the harness adds coverage without replacing any test.

#### Scenario: Audit finds no proxy to migrate

- **WHEN** the recipe tests are audited for assertions that only stand in for `MainController` signal-wiring behavior (grep for `activateRecipe`/`deactivateRecipe`/`stampActiveRecipe`/`dyeGrinderSetting`/`m_activeRecipe`)
- **THEN** none are found, and the harness coverage is recorded as 100% net-new (no test deleted)
