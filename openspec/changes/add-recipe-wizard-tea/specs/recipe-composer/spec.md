# recipe-composer Specification (delta)

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
