# recipe-composer

## ADDED Requirements

### Requirement: One composer window for create, edit, promote, and clone
The system SHALL provide a single recipe composer page used for all recipe creation and editing. It SHALL expose: name (required), profile picker (required, reusing the profile selector), bag picker (optional, reusing the change-beans dialog; selecting a bag links the recipe to that bag's bean), equipment picker (optional, reusing the switch-equipment dialog), dose, yield, temperature override, grind (showing "inherited from <bag>" by default with an explicit pin action), and the steam block (hasMilk, milk weight, pitcher, temperature/flow/timeout). Only name and profile SHALL be required to save.

#### Scenario: Blank creation
- **WHEN** the user creates a recipe from the Recipes page
- **THEN** the composer opens empty, and saving with just a name and profile succeeds

#### Scenario: Bag swap hints grind change
- **WHEN** the user changes the linked bag on an existing recipe with inherited grind
- **THEN** an inline hint shows that grind now follows the new bag and its current value

### Requirement: Promotion from shots
Shot History rows (beside the Load action), the Shot Detail page, and Auto-Favorites rows SHALL each offer a "create recipe from this shot" action that opens the composer prefilled from the shot record (profile, bean/bag, equipment, grinder setting, dose, yield/temperature overrides) and from the shot's steam snapshot when present (otherwise current steam settings). The `hasMilk` question SHALL be answerable in the composer before saving.

#### Scenario: Promote from history
- **WHEN** the user taps the promote action on a history row
- **THEN** the composer opens with all fields prefilled from that shot, and saving creates a recipe whose provenance records the source shot

#### Scenario: Promote an old-bag shot
- **WHEN** the promoted shot references a finished bag of a bean that has a newer open bag
- **THEN** the recipe links to the bean and resolves to the current open bag

### Requirement: Clone-and-edit
Every recipe SHALL offer a clone action that opens the composer as a copy of the source (all fields, including steam block and grind pin state), with the name field focused for immediate rename. The clone's provenance SHALL record the source recipe; the source's golden-shot link SHALL NOT be copied.

#### Scenario: Family variant in two edits
- **WHEN** the user clones "Morning capp", renames it, and changes only the milk weight
- **THEN** a second independent recipe exists and the original is unchanged

### Requirement: Composer respects the optionality ladder
The bag and equipment rows SHALL offer an explicit "none" state. A recipe saved without a bean SHALL keep grind as a recipe-local value; without equipment, the grind setting SHALL still be stored, unattributed. Missing rungs SHALL never block saving or activation.

#### Scenario: No equipment configured
- **WHEN** a user with no equipment packages creates a recipe
- **THEN** the equipment row shows "none", saving succeeds, and activation skips equipment selection
