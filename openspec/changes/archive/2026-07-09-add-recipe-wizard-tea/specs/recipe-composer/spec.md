# recipe-composer Specification (delta)

## ADDED Requirements

### Requirement: Recipe creation and editing is delivered by the recipe wizard
The single-window recipe composer SHALL be replaced by the drink-type-first recipe wizard (`recipe-wizard` capability). All creation and editing surfaces — blank create, promote-from-shot, and clone — SHALL route to the wizard, and `RecipeComposerPage.qml` SHALL NOT exist. The composer's field inventory (grind inherit/override, steam block, hot-water block with vessel-carried amounts and order) SHALL be preserved across the wizard's details and summary steps; the optionality ladder (no bean, no equipment) SHALL continue to hold.

#### Scenario: Every composer entry point opens the wizard
- **WHEN** the user creates, edits, promotes-from-shot, or clones a recipe
- **THEN** the recipe wizard opens (there is no separate composer page), with promote and clone landing on the wizard's summary step

## REMOVED Requirements

### Requirement: One composer window for create, edit, promote, and clone
**Reason**: The single-window composer is replaced by the drink-type-first recipe wizard.
**Migration**: All creation/edit surfaces route to the wizard (`recipe-wizard` capability). The composer's field inventory (grind inherit/override, steam block, hot-water block with vessel-carried amounts and order) is preserved across the wizard's details/summary steps; `RecipeComposerPage.qml` is deleted.

### Requirement: Promotion from shots
**Reason**: Superseded by the wizard's summary entry point.
**Migration**: The same promote actions (Shot History rows, Shot Detail page, Auto-Favorites rows) open the wizard directly on its summary step, prefilled from the shot record and snapshots — see `recipe-wizard` "Summary page is the edit surface".

### Requirement: Clone-and-edit
**Reason**: Superseded by the wizard's summary entry point.
**Migration**: Clone opens the wizard on the summary as a copy with the name focused; provenance semantics unchanged — see `recipe-wizard` "Summary page is the edit surface".

### Requirement: Composer respects the optionality ladder
**Reason**: The obligation moves to the wizard.
**Migration**: The wizard's bean step offers "No bean", the equipment row offers "none", and missing rungs never block saving or activation — see `recipe-wizard` bean-step and equipment requirements.
