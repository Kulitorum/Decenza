## MODIFIED Requirements

### Requirement: Drink-type-first step sequence
The system SHALL provide a recipe wizard as the single surface for recipe creation and editing. Creation SHALL walk: drink type → bean → profile → **equipment → dose/yield/temp/grind → steam and/or water (only the blocks the drink carries)** → summary. The drink types SHALL be espresso, filter, americano, long black, latte/cappuccino, latte + water, and tea. Picker steps (drink type, bean, profile) SHALL auto-advance on selection with no Next button; the post-profile windows SHALL be forms with an explicit Continue (the last one reads "Review" and leads to the summary). The **equipment window SHALL come first among the post-profile windows**, so the grinder — whose RPM capability gates the rpm field — is chosen before the dose/yield/temp/grind window. The post-profile windows SHALL be the SAME screens used when editing: creation walks them in order, while edit/clone/promote open on the summary and jump straight to a window from the tapped card, returning to the summary. Breadcrumb chips showing the drink/bean/profile selections so far SHALL provide back-navigation; the bottom-bar back arrow SHALL step back through the windows in reverse.

#### Scenario: Equipment window comes before the numbers window
- **WHEN** the user taps Espresso, then a bag, then a profile
- **THEN** the wizard is on the equipment window, and continuing from it reaches the dose/yield/temp/grind window — the grinder is chosen before grind/rpm

#### Scenario: Only the blocks the drink has appear as windows
- **WHEN** the user creates a latte (milk, no water)
- **THEN** the walk includes a steam window and no water window; an americano's walk includes a water window and no steam window; a plain espresso's walk ends at the numbers window before the summary

#### Scenario: Latte + Water walk includes both blocks
- **WHEN** the user picks the "Latte + Water" drink type
- **THEN** the walk includes BOTH a steam window and a water window, and the recipe saves with drink type `latte_hotwater`, a milk block, and a hot-water block (order "before")
- **AND** the water window SHALL NOT present a before/after order choice for this type

#### Scenario: Breadcrumb returns to an earlier step
- **WHEN** the user taps the bean chip while on a post-profile window
- **THEN** the bean step reopens, and a new selection returns to the walk with dependent state updated

### Requirement: Drink-type templates set defaults without restricting composition
Each drink type SHALL configure the wizard via a static template: the profile beverage-type filter set, the bag kind filter, block pre-seeds (latte/cappuccino pre-enables the steam block with milk; americano pre-enables the hot-water block with order "after"; long black with order "before"; latte + water pre-enables BOTH the steam block with milk AND the hot-water block with order "before", and does NOT offer the before/after order choice — the order is fixed), and the details-step field list. The stored blocks SHALL remain the sole source of truth for machine behavior. The summary SHALL offer add/remove affordances for the milk and hot-water blocks regardless of template, so any block combination expressible in the recipe model remains creatable.

#### Scenario: Latte template pre-seeds milk
- **WHEN** the user picks Latte/Cappuccino
- **THEN** the details step includes the milk fields and the saved recipe carries a steam block with hasMilk true

#### Scenario: Latte + Water template pre-seeds both blocks
- **WHEN** the user picks Latte + Water
- **THEN** the details walk includes both the milk fields and the water-vessel picker (with no before/after order choice), and the saved recipe carries a steam block with hasMilk true AND a hot-water block with hasWater true and order "before"

#### Scenario: Template escape hatch
- **WHEN** the user creates an Espresso recipe and, on the summary, adds the hot-water block
- **THEN** the recipe saves with both drink type "espresso" and a hot-water block, and activation behaves per the blocks
