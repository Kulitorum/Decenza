# recipe-composer Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipe creation and editing is delivered by the recipe wizard
The single-window recipe composer SHALL be replaced by the drink-type-first recipe wizard (`recipe-wizard` capability). All creation and editing surfaces — blank create, promote-from-shot, and clone — SHALL route to the wizard, and `RecipeComposerPage.qml` SHALL NOT exist. The composer's field inventory (grind inherit/override, steam block, hot-water block with vessel-carried amounts and order) SHALL be preserved across the wizard's details and summary steps; the optionality ladder (no bean, no equipment) SHALL continue to hold.

#### Scenario: Every composer entry point opens the wizard
- **WHEN** the user creates, edits, promotes-from-shot, or clones a recipe
- **THEN** the recipe wizard opens (there is no separate composer page), with promote and clone landing on the wizard's summary step

