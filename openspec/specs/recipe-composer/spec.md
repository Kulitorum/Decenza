# recipe-composer Specification

## Purpose

Governs how drink recipes are created and edited. The drink-type-first recipe wizard replaces the old single-window composer, and every create, edit, promote-from-shot and clone entry point opens it.

## Requirements
### Requirement: Recipe creation and editing is delivered by the recipe wizard
The single-window recipe composer SHALL be replaced by the drink-type-first recipe wizard (`recipe-wizard` capability), and `RecipeComposerPage.qml` SHALL NOT exist. Blank create, promote-from-shot and clone SHALL all route to the wizard. The composer's field inventory SHALL be preserved across the wizard's details and summary steps, and the optionality ladder (no bean, no equipment) SHALL continue to hold.

#### Scenario: Every composer entry point opens the wizard
- **WHEN** the user creates, edits, promotes-from-shot, or clones a recipe
- **THEN** the recipe wizard opens (there is no separate composer page), with promote and clone landing on the wizard's summary step

#### Scenario: Composer field inventory survives the wizard
- **WHEN** a recipe is edited in the wizard
- **THEN** grind inherit/override, the steam block, and the hot-water block (vessel-carried amounts and order) are all present across the details and summary steps

