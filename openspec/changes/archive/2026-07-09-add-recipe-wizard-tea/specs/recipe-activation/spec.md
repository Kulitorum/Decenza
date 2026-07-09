# recipe-activation Specification (delta)

## ADDED Requirements

### Requirement: Profile-less recipes activate without the profile pipeline
When an activated recipe has no profile, the activation path SHALL skip the profile-load, dose-write, and yield/temperature-override stages entirely and SHALL still apply the bean link (open-bag resolution), equipment selection, and the hot-water block (vessel re-select by name with snapshot recreation, then hot-water settings apply). No steam-heater hold SHALL be taken (the existing hot-water rule). The currently loaded espresso profile SHALL be left untouched. `recipeActivated` SHALL fire with the same terminal semantics as profile-carrying recipes, and `activeRecipeId` SHALL be set last.

#### Scenario: Hot-water tea activation
- **WHEN** a profile-less tea recipe is activated from any surface
- **THEN** the tea bag becomes active, the snapshotted vessel is selected and its values applied, the loaded profile is unchanged, and no steam heating starts

#### Scenario: Zero shots forever
- **WHEN** the user brews hot water repeatedly with a profile-less recipe active
- **THEN** the recipe accumulates no shots and remains hard-deletable
