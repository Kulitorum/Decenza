# recipe-activation Delta

## MODIFIED Requirements

### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline (`applyLoadedShotMetadata` semantics: profile by title with stored-JSON fallback, bag selected before DYE field writes, queued dose write). QML pill taps, the MCP `recipe_activate` tool, and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly (no bean-identity resolution at activation time).

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, equipment package is selected, dose/yield/temperature apply, grind resolves per inherit-or-pin against the linked bag, and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via QML, MCP, or the web API
- **THEN** the resulting app and machine state are identical

### Requirement: Profile-less recipes activate without the profile pipeline
When an activated recipe has no profile, the activation path SHALL skip the profile-load, dose-write, and yield/temperature-override stages entirely and SHALL still apply the linked bag, equipment selection, and the hot-water block (vessel re-select by name with snapshot recreation, then hot-water settings apply). No steam-heater hold SHALL be taken (the existing hot-water rule). The currently loaded espresso profile SHALL be left untouched. `recipeActivated` SHALL fire with the same terminal semantics as profile-carrying recipes, and `activeRecipeId` SHALL be set last.

#### Scenario: Hot-water tea activation
- **WHEN** a profile-less tea recipe is activated from any surface
- **THEN** the tea bag becomes active, the snapshotted vessel is selected and its values applied, the loaded profile is unchanged, and no steam heating starts

#### Scenario: Zero shots forever
- **WHEN** the user brews hot water repeatedly with a profile-less recipe active
- **THEN** the recipe accumulates no shots and remains hard-deletable

## ADDED Requirements

### Requirement: Stale recipes activate with the finished bag's data
Activating a recipe whose linked bag is finished SHALL apply the full bundle: profile, dose/yield/temperature, equipment, steam, and hot water apply normally, and grind resolves per inherit-or-pin against the finished bag (its last dial is a better starting point than nothing). The finished bag SHALL NOT be returned to inventory by activation. Activation SHALL succeed with no error or dialog; the stale indication remains a display concern of the listing surfaces.

#### Scenario: Pulling a shot from a stale recipe
- **WHEN** the user activates a stale recipe and starts espresso
- **THEN** the shot runs with the recipe's profile, numbers, and the finished bag's grind — identical to activation before the bag was finished
