## ADDED Requirements

### Requirement: Auto-load recipe setting

The system SHALL persist a single, optional auto-load recipe id, sharing the existing `Settings.app.autoLoadRevertMinutes` timeout with profile auto-load.

#### Scenario: Default state

- **WHEN** the user has never configured a recipe auto-load
- **THEN** `Settings.dye.autoLoadRecipeId` is `-1`

#### Scenario: Setting a recipe replaces any prior recipe auto-load

- **WHEN** an auto-load is already configured for recipe A and the user sets the auto-load to recipe B
- **THEN** `Settings.dye.autoLoadRecipeId` equals B's id and the previous selection on A is no longer in effect

#### Scenario: Clearing the auto-load preserves the shared timeout

- **WHEN** the user clears the recipe auto-load
- **THEN** `Settings.dye.autoLoadRecipeId` is `-1` AND `Settings.app.autoLoadRevertMinutes` retains its prior value

#### Scenario: autoLoadRecipeId is a device-local id, not exported

- **WHEN** the user exports a settings bundle while a recipe auto-load is configured
- **THEN** `autoLoadRecipeId` is NOT included in the export, matching the existing treatment of `dye/activeBagId`, `dye/activeEquipmentId`, and `dye/activeRecipeId` (all device-local DB row ids meaningless on another device)

#### Scenario: The shared revert timeout still round-trips

- **WHEN** the user exports a settings bundle and imports it on another device
- **THEN** the imported device's `autoLoadRevertMinutes` matches the exported value (it is already exported under `profile.autoLoadRevertMinutes`, shared with the profile side)

### Requirement: Mutual exclusion with profile auto-load

Exactly one of {a profile, a recipe} SHALL be auto-loadable at a time. Setting either target SHALL silently clear the other, with no confirmation step.

#### Scenario: Setting a recipe auto-load clears an existing profile auto-load

- **WHEN** `Settings.app.autoLoadProfileFilename` is non-empty AND the user sets `Settings.dye.autoLoadRecipeId` to a valid recipe id
- **THEN** `Settings.dye.autoLoadRecipeId` equals the new id AND `Settings.app.autoLoadProfileFilename` becomes `""`

#### Scenario: Setting a profile auto-load clears an existing recipe auto-load

- **WHEN** `Settings.dye.autoLoadRecipeId` is not `-1` AND the user sets `Settings.app.autoLoadProfileFilename` to a valid filename
- **THEN** `Settings.app.autoLoadProfileFilename` equals the new filename AND `Settings.dye.autoLoadRecipeId` becomes `-1`

#### Scenario: Clearing one target does not affect the other

- **WHEN** `Settings.dye.autoLoadRecipeId` is `-1` (no recipe auto-load configured) AND the user clears the profile auto-load
- **THEN** `Settings.dye.autoLoadRecipeId` remains `-1` (unchanged, not re-triggered)

#### Scenario: No confirmation shown when displacing the other target

- **WHEN** the user sets an auto-load target that displaces the other kind's existing auto-load
- **THEN** the change takes effect immediately with a toast naming what changed, without a confirmation dialog

### Requirement: Auto-load entry point

`MainController` SHALL expose a single entry point that decides whether to load the configured auto-load recipe and handles stale-target cleanup, mirroring `ProfileManager::loadAutoLoadProfileIfNeeded`.

#### Scenario: No-op when no recipe auto-load configured

- **WHEN** `loadAutoLoadRecipeIfNeeded` is called AND `autoLoadRecipeId` is `-1`
- **THEN** the system does nothing and the active recipe is unchanged

#### Scenario: No-op when the auto-load is already active

- **WHEN** `loadAutoLoadRecipeIfNeeded` is called AND `autoLoadRecipeId` equals `Settings.dye.activeRecipeId`
- **THEN** the system does not call `activateRecipe` and the active recipe is unchanged

#### Scenario: Stale target is cleared

- **WHEN** `loadAutoLoadRecipeIfNeeded` is called AND `autoLoadRecipeId` does not resolve to an existing, non-archived recipe
- **THEN** the system clears `autoLoadRecipeId` AND emits a stale-cleared signal so the UI can show a toast

#### Scenario: Successful auto-load

- **WHEN** `loadAutoLoadRecipeIfNeeded` is called AND `autoLoadRecipeId` resolves to an existing, non-archived recipe AND that recipe is not the active recipe
- **THEN** the system calls `MainController::activateRecipe(autoLoadRecipeId)`

### Requirement: Trigger — app startup

The system SHALL invoke the recipe auto-load entry point once during startup, alongside the existing profile auto-load invocation.

#### Scenario: Auto-load fires on cold launch

- **WHEN** the app starts AND a recipe auto-load is configured AND the active recipe differs from the configured id
- **THEN** the configured recipe is activated before the user interacts with the app

#### Scenario: Both entry points are invoked but only one acts

- **WHEN** the app starts
- **THEN** both `ProfileManager.loadAutoLoadProfileIfNeeded()` and `MainController.loadAutoLoadRecipeIfNeeded()` are called AND at most one of them performs a load, since the two settings are mutually exclusive

### Requirement: Trigger — DE1 wake from sleep

The system SHALL invoke the recipe auto-load entry point on every `DE1Device` state transition from `Sleep` to `Idle` that represents a genuine user-initiated wake, using the same reconnect-guard tracking already used for profile auto-load.

#### Scenario: Wake from sleep reloads the auto-load recipe

- **WHEN** `DE1Device.state` transitions from `Sleep` to `Idle` while the device has remained connected AND a recipe auto-load is configured AND it differs from the active recipe
- **THEN** the configured recipe is activated

#### Scenario: BLE reconnect does not false-fire

- **WHEN** `DE1Device.connected` toggles (reconnect) AND the first state notification after reconnect shows `Idle`
- **THEN** the recipe auto-load entry point is NOT invoked from the state-change path

### Requirement: Trigger — inactivity on the Idle page

The system SHALL invoke the recipe auto-load entry point after `Settings.app.autoLoadRevertMinutes` of continuous inactivity while the Idle page is displayed, reusing the existing idle-countdown timer used for profile auto-load.

#### Scenario: Idle timeout reloads the auto-load recipe

- **WHEN** the current page is `idlePage` AND no user activity has been registered for `autoLoadRevertMinutes` minutes AND no machine operation is active AND a recipe auto-load is configured
- **THEN** the configured recipe is activated AND the inactivity countdown resets

#### Scenario: Countdown reset applies to either target

- **WHEN** the user changes `Settings.dye.autoLoadRecipeId` (set or clear)
- **THEN** the inactivity countdown resets, exactly as changing `Settings.app.autoLoadProfileFilename` already does

#### Scenario: Zero disables the inactivity trigger for recipes too

- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the inactivity trigger is disabled for both the recipe and profile targets AND the app-startup and wake-from-sleep triggers continue to function

### Requirement: Recipe card auto-load button

Each non-archived recipe card SHALL show a pin-style icon button in its action row, positioned immediately before the Edit button, that toggles that recipe's auto-load state immediately with no confirmation dialog.

#### Scenario: Button present on non-archived cards

- **WHEN** the recipe list renders a card for a non-archived recipe
- **THEN** a `pin.svg` icon button appears as the first action, to the left of the Edit button

#### Scenario: Button absent on archived cards

- **WHEN** the recipe list renders a card for an archived recipe
- **THEN** the auto-load button is not shown, matching the existing Edit/Clone visibility rule for archived cards

#### Scenario: Button reflects current auto-load state

- **WHEN** the card's recipe id equals `Settings.dye.autoLoadRecipeId`
- **THEN** the button is tinted `Theme.primaryColor`; otherwise it renders in its default outline/untinted state

#### Scenario: Tapping the button when inactive sets auto-load

- **WHEN** the user taps the button on a card that is not the current auto-load
- **THEN** `Settings.dye.autoLoadRecipeId` is set to that recipe's id immediately AND a toast confirms the change AND any prior profile or recipe auto-load is cleared per the mutual-exclusion requirement

#### Scenario: Tapping the button when active clears auto-load

- **WHEN** the user taps the button on the card that is the current auto-load
- **THEN** `Settings.dye.autoLoadRecipeId` is set to `-1` AND a toast confirms auto-load was disabled

#### Scenario: Button has an accessible name reflecting state

- **WHEN** an accessibility screen reader focuses the button
- **THEN** the reader announces "Set auto-load" when inactive or "Disable auto-load" when active for that recipe

### Requirement: Recipe card auto-load status row

The card that is the current auto-load target SHALL show a compact status row distinct from the toggle button, spelling out "Auto-load" in words so the state does not depend on recognizing the pin icon on sight.

#### Scenario: Status row shown only on the auto-load target card

- **WHEN** a recipe card's `recipe.id` equals `Settings.dye.autoLoadRecipeId`
- **THEN** a status row appears in the card showing a pin icon, an "Auto-load" text label, and a revert-minutes stepper

#### Scenario: Status row hidden on all other cards

- **WHEN** a recipe card's `recipe.id` does not equal `Settings.dye.autoLoadRecipeId` (including when no recipe auto-load is configured, or when a profile is the auto-load target)
- **THEN** the status row is not rendered

#### Scenario: No recipe name repeated

- **WHEN** the status row is shown
- **THEN** it does not repeat the recipe's name, which is already shown as the card's own title

#### Scenario: No separate clear control

- **WHEN** the status row is shown
- **THEN** it does not include its own clear/× button — clearing auto-load is done via the card's existing toggle button (see "Tapping the button when active clears auto-load")

#### Scenario: Editing revert minutes from the card updates the shared setting live

- **WHEN** the user changes the value in the status row's stepper
- **THEN** `Settings.app.autoLoadRevertMinutes` is updated live AND any in-progress inactivity countdown is reset to the new value, matching the equivalent control on `ProfileSelectorPage`'s status strip

#### Scenario: "off" rendering at zero

- **WHEN** `autoLoadRevertMinutes` is `0`
- **THEN** the stepper displays "off" instead of "0 min", matching the profile strip's convention

### Requirement: Eligibility limited to existing, non-archived recipes

The system SHALL enforce that only an existing, non-archived recipe can be the auto-load target, and SHALL gracefully recover when the auto-loaded recipe is archived or deleted.

#### Scenario: Auto-load is cleared when its recipe is archived

- **WHEN** `RecipeStorage::requestArchiveRecipe` is called with the id currently set as `autoLoadRecipeId`
- **THEN** `autoLoadRecipeId` is cleared to `-1` immediately, without waiting for the next trigger

#### Scenario: Auto-load is cleared when its recipe is deleted

- **WHEN** `RecipeStorage::requestDeleteRecipe` is called with the id currently set as `autoLoadRecipeId`
- **THEN** `autoLoadRecipeId` is cleared to `-1` immediately

#### Scenario: Toast on stale clear at trigger time

- **WHEN** a trigger fires AND `autoLoadRecipeId` no longer resolves to an existing, non-archived recipe
- **THEN** the user sees a toast that the auto-load recipe is no longer available AND the setting is cleared

### Requirement: MCP — get auto-load

The MCP server SHALL expose a `recipe_get_auto_load` tool returning the current recipe auto-load configuration with a read access level.

#### Scenario: Configured auto-load is reported

- **WHEN** an MCP client calls `recipe_get_auto_load` AND a recipe auto-load is configured
- **THEN** the response includes `recipeId`, `name`, and `revertMinutes`

#### Scenario: No auto-load configured

- **WHEN** an MCP client calls `recipe_get_auto_load` AND `autoLoadRecipeId` is `-1`
- **THEN** the response includes `recipeId: null` AND `revertMinutes` (the current configured timeout)

### Requirement: MCP — set auto-load

The MCP server SHALL expose a `recipe_set_auto_load` tool with a settings access level that pins a recipe as the auto-load and optionally updates the shared revert minutes, clearing any profile auto-load in the same call.

#### Scenario: Successful set

- **WHEN** the client calls `recipe_set_auto_load` with a `recipeId` that exists and is not archived
- **THEN** the response is `{ success: true, recipeId, name, revertMinutes }` AND the setting is persisted on the GUI thread AND `Settings.app.autoLoadProfileFilename` is cleared to `""`

#### Scenario: recipeId missing

- **WHEN** the client calls `recipe_set_auto_load` with an absent `recipeId`
- **THEN** the response is `{ error: "recipeId is required" }` AND no state changes

#### Scenario: recipeId not found

- **WHEN** the client calls `recipe_set_auto_load` with a `recipeId` that does not exist
- **THEN** the response is `{ error: "Recipe not found: <recipeId>" }` AND no state changes

#### Scenario: recipeId is archived

- **WHEN** the client calls `recipe_set_auto_load` with a `recipeId` that exists but is archived
- **THEN** the response is `{ error: "Recipe is archived" }` AND no state changes

#### Scenario: Optional revert minutes updates the shared setting

- **WHEN** the client supplies `revertMinutes` alongside `recipeId`
- **THEN** `Settings.app.autoLoadRevertMinutes` (clamped to 0..60) is updated, the same shared key the profile tools use

### Requirement: MCP — clear auto-load

The MCP server SHALL expose a `recipe_clear_auto_load` tool with a settings access level that disables the recipe auto-load without affecting the shared revert timeout.

#### Scenario: Successful clear

- **WHEN** the client calls `recipe_clear_auto_load`
- **THEN** `autoLoadRecipeId` is set to `-1` AND `autoLoadRevertMinutes` is unchanged AND the response is `{ success: true }`
