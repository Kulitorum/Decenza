# recipe-quick-switch Specification

## Purpose
Covers the Recipes idle widget (a Beans-style pill row of most-recently-used recipes, paginated within two rows, that activates a recipe on tap), bag coherence on activation, the Recipes management page and its card contents, the resulting-value shot-plan line, drink-type icons, and the stale-recipe indication.
## Requirements
### Requirement: Recipes idle widget mirrors the Beans button

The Recipes layout widget SHALL mirror the Beans widget: tap toggles a pill row of the most-recently-used non-archived recipes, tapping a pill activates that recipe, and double-tap or long-press opens the Recipes management page. The active recipe's pill SHALL be highlighted. With zero recipes, a plain tap SHALL open the management page directly. The widget SHALL meet the Beans widget's accessibility rules.

#### Scenario: Quick switch
- **WHEN** the user taps the Recipes widget and selects a pill
- **THEN** that recipe activates (full bundle incl. steam) and the pill row closes with the selection highlighted on next open

#### Scenario: Empty state
- **WHEN** the user taps the widget with zero recipes
- **THEN** the Recipes management page opens directly

#### Scenario: MRU ordering
- **WHEN** a recipe is activated
- **THEN** it moves to the front of the pill list, and the first page shows the most-recent recipes that fit within two rows

#### Scenario: Page size follows name length
- **WHEN** recipes have long descriptive names (bean + type + profile) such that fewer fit within two rows
- **THEN** the first page shows only as many as fit, the rest paginate, and different pages MAY show different numbers of recipes

#### Scenario: Paging to reach older recipes
- **WHEN** more recipes exist than fit within two rows and the user taps the next arrow
- **THEN** the row shows the next group of recipes (however many fit) in MRU order without activating any recipe or changing the current selection

#### Scenario: Arrows appear only as appropriate
- **WHEN** the recipe pill row is on the first page
- **THEN** the previous arrow SHALL be hidden
- **AND** the next arrow SHALL be shown only if more recipes exist than fit within two rows

#### Scenario: Everything fits needs no arrows
- **WHEN** every recipe fits within two rows at the current width
- **THEN** the pill row SHALL show no pagination arrows and appear exactly as the non-paginated row did

#### Scenario: Never more than two rows
- **WHEN** any page of recipe pills is shown
- **THEN** the pills SHALL occupy at most two rows

### Requirement: Paging SHALL change only which recipes are visible

The previous arrow SHALL show only when a previous page exists, and the next arrow only when a further page exists. Paging SHALL change only which recipes are visible; it SHALL NOT activate a recipe, change the selection or reorder the list.

#### Scenario: Paging does not activate a recipe

- **WHEN** the user pages the pill row
- **THEN** no recipe SHALL be activated and the selection SHALL be unchanged

### Requirement: The pill row SHALL fit recipes within two rows

The pill row SHALL show as many recipes as fit within at most two rows at the current width. The count SHALL be computed live from measured pill widths, so it MAY differ between pages. Opening the widget SHALL start on the first page, and the current page SHALL be clamped to range when the recipe list changes.

#### Scenario: Page size follows measured widths

- **WHEN** recipe names are long enough that fewer fit within two rows
- **THEN** each page SHALL hold only as many recipes as fit at the measured width

### Requirement: Bean button coherence
Activating a recipe SHALL set the active bag (the recipe's linked bag), so the Beans widget's pill selection reflects the recipe's bag without additional wiring. Deactivation by ingredient swap SHALL deselect the recipe pill while leaving bag selection as the user set it.

#### Scenario: Bag pill follows recipe
- **WHEN** a recipe linked to bag X is activated
- **THEN** the Beans widget shows bag X as selected

### Requirement: Management page

The Recipes management page SHALL list all non-archived recipes, with create, edit, clone and archive actions, delete only for recipes with no shots, and access to archived recipes. When zero recipes exist it SHALL show two large starter tiles in place of a text hint: one opening shot history to promote a good shot, and one opening the wizard.

#### Scenario: Archive from management page
- **WHEN** the user archives a used recipe
- **THEN** it disappears from the list default view and the MRU pills, and remains visible in shot history provenance

#### Scenario: Same-bean twins are distinguishable
- **WHEN** two recipes share a bean but differ by profile
- **THEN** each card shows its profile on the drink line without truncation

#### Scenario: Latte card carries its milk
- **WHEN** a latte recipe stores 200g of milk
- **THEN** its card's drink line includes "200g milk" and no bare "milk" token

#### Scenario: Hot-water tea card degrades deliberately
- **WHEN** a profile-less hot-water tea recipe is listed
- **THEN** its card shows "Tea · Hot water" and the vessel's amount and temperature instead of an empty shot plan

#### Scenario: First-run empty state teaches both paths
- **WHEN** the management page opens with zero recipes
- **THEN** the user sees a "start from a good shot" tile opening shot history and a "build from scratch" tile opening the wizard

#### Scenario: Cards are immune to the loaded profile
- **WHEN** the recipes list is open and a recipe on profile A (frames 84 · 94°C) is displayed while the machine currently holds profile B (frames 90 · 88°C)
- **THEN** that recipe's card shows values resolved from profile A's own frames, and activating a different recipe changes no other card's temperatures

#### Scenario: A card shows its temperature as a resulting baseline
- **WHEN** a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C is listed
- **THEN** its card's temperature reads "81 · 91°C" in the default text color, with no separate offset tag
- **AND** the live Shot Plan for that recipe (when active) reads the same "81 · 91°C" — the card and the live widget agree

#### Scenario: A card shows its yield as the plain resulting value
- **WHEN** a recipe stores yield 40 on a profile whose target weight is 36
- **THEN** its card's yield reads "40.0g" in the default text color, with no arrow and no highlight

#### Scenario: An unmodified value carries no highlight
- **WHEN** a recipe stores offset 0 and a yield equal to its profile's target
- **THEN** its card shows the profile's temps and yield in the default text color with no tag and no arrow

### Requirement: Recipe cards SHALL be unaffected by other profiles and recipes

Activating, loading or editing a different profile or recipe SHALL NOT change what any other recipe's card displays. The wizard's summary preview SHALL use the same shot-plan rule.

#### Scenario: Wizard preview uses the card rule

- **WHEN** the wizard's summary preview is shown for a recipe
- **THEN** its temperature and yield SHALL be resolved by the same rule as the recipe card

### Requirement: The shot-plan line SHALL show the recipe's own resulting values

The shot-plan line SHALL render the recipe's resulting temperature and yield as a plain baseline, with no delta tag or arrow. The temperature SHALL come from that recipe's own profile frames, shifted by `tempOffsetC`. The yield SHALL be the stored yield, else the profile's target. The temperature SHALL be omitted when the profile resolves by neither title nor embedded JSON, and SHALL NEVER fall back to the loaded profile's frames.

#### Scenario: Unresolved profile omits the temperature

- **WHEN** a recipe's profile resolves by neither title nor embedded JSON
- **THEN** its card SHALL omit the temperature segment and SHALL NOT show the loaded profile's temperatures

### Requirement: Recipe cards SHALL present their lines in a fixed order

Each card SHALL show, in order: the recipe name with the Active badge; a drink line with the drink-type icon, short label, profile title and milk weight when stored; a bean line with bag name and shot count; then the shot-plan line. Card text SHALL wrap rather than elide. A profile-less hot-water tea card SHALL show "Tea · Hot water" and the vessel snapshot in place of the shot-plan line.

#### Scenario: Card lines follow the fixed order

- **WHEN** a recipe card is shown
- **THEN** its lines SHALL appear in the order name, drink, bean, shot plan

### Requirement: Recipe pills show a drink-type icon
Recipe pills in the idle widget and recipe lists SHALL show a small icon for the recipe's drink type (stored value, derived from blocks when absent), rendered as an SVG image (never a Unicode glyph per QML conventions). Wherever the drink type appears as text (cards, wizard summary, auto-names), surfaces SHALL use short labels — "Latte", "Tea", "Americano", "Long black" — reserving the long picker labels ("Latte / Cappuccino", "Tea (hot water)") for the wizard's drink-type step.

#### Scenario: Mixed pill row is scannable
- **WHEN** the idle widget shows an espresso, a latte, and a tea recipe
- **THEN** each pill carries its distinct drink-type icon

#### Scenario: Americano and long black stay distinct
- **WHEN** an americano recipe and a long-black recipe appear on cards or the summary
- **THEN** each pairs the shared water icon with its own short text label

### Requirement: Stale recipes are visibly indicated
Surfaces listing recipes SHALL indicate a stale recipe (linked bag finished): the management card SHALL show a "bag finished" state with the one-tap re-point affordance (see `recipe-bag-lifecycle`), and the idle pill SHALL be dimmed or badged. Indication SHALL NOT block activation.

#### Scenario: Stale pill still works
- **WHEN** the idle pill row contains a stale recipe
- **THEN** the pill is visually distinct, and tapping it still activates the recipe

