## MODIFIED Requirements

### Requirement: One composer window for create, edit, promote, and clone
The system SHALL provide a single recipe composer page used for all recipe creation and editing. It SHALL expose: name (required), profile picker (required, with a search box), bag picker (optional; selecting a bag links the recipe to that bag's bean), equipment picker (optional), dose, yield, temperature offset, grind (showing "follows the bean" by default with an explicit override that covers grind + rpm), the steam block (hasMilk, milk weight, pitcher, duration/flow/temperature), and the hot-water block (an "add hot water" toggle, a water-vessel picker whose selected vessel supplies the amount, temperature, and flow — no separate amount field — and an order choice of water before the espresso for a long black or after it for an Americano). The pickers are lightweight in-page list dialogs. Only name and profile SHALL be required to save.

#### Scenario: Blank creation
- **WHEN** the user creates a recipe from the Recipes page
- **THEN** the composer opens empty, and saving with just a name and profile succeeds

#### Scenario: Bag swap hints grind change
- **WHEN** the user changes the linked bag on an existing recipe with inherited grind
- **THEN** an inline hint shows that grind now follows the new bag and its current value

#### Scenario: Americano is composable
- **WHEN** the user turns on added hot water and picks a water vessel alongside an espresso profile
- **THEN** saving persists the hot-water block (the vessel snapshot) on the recipe

### Requirement: Promotion from shots
Shot History rows (beside the Load action), the Shot Detail page, and Auto-Favorites rows SHALL each offer a "create recipe from this shot" action that opens the composer prefilled from the shot record (profile, bean/bag, equipment, grinder setting, dose, yield/temperature overrides) and from the shot's steam and hot-water snapshots when present (otherwise current steam and hot-water settings). The `hasMilk` question SHALL be answerable in the composer before saving.

#### Scenario: Promote from history
- **WHEN** the user taps the promote action on a history row
- **THEN** the composer opens with all fields prefilled from that shot, including any hot-water snapshot, and saving creates a recipe whose provenance records the source shot

#### Scenario: Promote an old-bag shot
- **WHEN** the promoted shot references a finished bag of a bean that has a newer open bag
- **THEN** the recipe links to the bean and resolves to the current open bag

### Requirement: Clone-and-edit
Every recipe SHALL offer a clone action that opens the composer as a copy of the source (all fields, including steam block, hot-water block, and grind pin state), with the name field focused for immediate rename. The clone's provenance SHALL record the source recipe; the source's golden-shot link SHALL NOT be copied.

#### Scenario: Family variant in two edits
- **WHEN** the user clones "Morning capp", renames it, and changes only the milk weight
- **THEN** a second independent recipe exists and the original is unchanged
