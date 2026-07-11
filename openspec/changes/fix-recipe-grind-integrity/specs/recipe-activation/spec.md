## MODIFIED Requirements

### Requirement: Single activation path shared by all surfaces
Recipe activation SHALL be implemented once in the main controller, reusing the existing shot-load pipeline (`applyLoadedShotMetadata` semantics: profile by title with stored-JSON fallback, bag selected before DYE field writes, queued dose write). QML pill taps, the MCP `recipe_activate` tool, and the ShotServer activate route SHALL all call this single path. The active recipe id SHALL live in the DYE settings domain beside `activeBagId`. The bag stage SHALL select the recipe's linked bag directly (no bean-identity resolution at activation time).

#### Scenario: Activation applies the full bundle
- **WHEN** a recipe is activated from any surface
- **THEN** the profile is loaded, the linked bag becomes active, equipment package is selected, dose/yield/temperature apply, grind/rpm apply from the recipe's own owned values (recipe-model), and steam settings are written

#### Scenario: Identical semantics across surfaces
- **WHEN** the same recipe is activated via QML, MCP, or the web API
- **THEN** the resulting app and machine state are identical

### Requirement: Optionality ladder is never violated
No brewing, steaming, or navigation flow SHALL require a recipe. With no recipe active, all settings (including steam) SHALL behave exactly as before this change. Activating a recipe that lacks optional rungs SHALL apply what the recipe has: a missing equipment rung leaves the current equipment untouched, while a missing bean rung SHALL **clear the active bag** — the session moves to the "no beans selected" state rather than staying attributed to whatever bag happened to be active before. The system SHALL NOT prompt users to create recipes.

#### Scenario: Recipe-less user
- **WHEN** a user never creates a recipe
- **THEN** every existing flow (profile selection, bag write-through, global steam settings) is byte-for-byte unchanged

#### Scenario: Bean-less recipe activation clears the bag
- **WHEN** a recipe with no linked bean is activated while some bag is active
- **THEN** profile, dose, steam, and the recipe's own grind apply, and the active bag is cleared ("no beans selected")
- **AND** subsequent grind edits and the shot's bean attribution touch no bag at all

#### Scenario: Clearing the bag does not deactivate the bean-less recipe
- **WHEN** a bean-less recipe's activation clears the active bag
- **THEN** the recipe remains the active recipe — the ingredient-swap deactivation watcher treats "no bag" as matching a recipe that has no bag

## ADDED Requirements

### Requirement: Same-recipe re-activation does not race in-flight edits
Re-activating a recipe that is already the active recipe SHALL NOT overwrite a field whose edit is still being persisted (a write-through to that same recipe row that has not yet been acknowledged). Re-activation of any *other* recipe, or first activation of the currently-active recipe id from a fresh app session, is unaffected and always applies the full bundle from the current stored state.

#### Scenario: Re-tapping the active recipe pill preserves an unsent edit
- **WHEN** the user edits the active recipe's grind, and before that edit is acknowledged by storage the user re-taps the same recipe's pill (triggering re-activation)
- **THEN** the live grind value after re-activation is the user's edit, not a stale pre-edit value

#### Scenario: Re-activation still reflects a settled external edit
- **WHEN** the active recipe was edited from another client (MCP or web) and no local write to that recipe is in flight
- **THEN** re-activating the recipe (or any trigger that re-applies it) reflects the externally-made change
