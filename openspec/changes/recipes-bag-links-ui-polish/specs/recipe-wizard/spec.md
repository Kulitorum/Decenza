# recipe-wizard Delta

## MODIFIED Requirements

### Requirement: Bean step filters by bag kind and is skippable
The bean step SHALL list open bags whose kind matches the drink type (tea → tea bags; all other types → coffee bags) as a tile grid in the same visual language as the drink-type step: each open bag is its own tile carrying the bag photo (from the existing bean-image cache), the roaster as a caption, the coffee name, and the bag's roast date or age — so two bags of the same bean are visibly distinct choices. Bags SHALL NOT be deduplicated by bean. "Add a new coffee…" (or tea) and "No bean" SHALL render as ghost tiles (dashed border, same size) at the end of the grid. Selecting a tile SHALL link that specific bag. Skipping via "No bean" SHALL produce a bag-less recipe per the optionality ladder (grind stored recipe-locally for coffee drinks).

#### Scenario: Two bags of the same bean are distinguishable
- **WHEN** two open bags of the same bean with different roast dates appear on the bean step
- **THEN** each shows its own tile with its roast date/age, and selecting one links exactly that bag

#### Scenario: Tea drink shows only tea bags
- **WHEN** the user picks Tea and reaches the bean step
- **THEN** only bags with kind "tea" are listed

#### Scenario: No bean
- **WHEN** the user chooses "No bean"
- **THEN** the wizard advances to the profile step and the saved recipe has no bag link

### Requirement: Profile step filters by drink type and ranks by history
The profile step SHALL show only profiles whose `beverage_type` matches the drink type's filter set: espresso, americano, long black, and latte/cappuccino → `espresso` (a missing or empty `beverage_type` SHALL be treated as espresso); filter → `filter` and `pourover`; tea → `tea_portafilter`. Maintenance beverage types SHALL never appear. Profiles SHALL be presented in ranked tiers with visible headers: ① profiles used with this bean (exact bean identity match in shot history, most recent first), ② knowledge-driven recommendations and similar beans — coffee: profiles whose knowledge-base entry states an affinity for the bag's roast level (KB `roastAffinity`, authored only from each profile's own dial-in documentation, shown with a "suits <roast> roasts" reason label) rank first, then profiles used with same-roast-level beans; tea: type-matches between the bag's tea type and stock tea profile names rank first with a reason label, then same-tea-type history. The recommended tier SHALL be capped to a handful (5); candidates beyond the cap fall through to the final tier — ③ all remaining profiles in the filter set (coffee: alphabetical; tea: ordered by proximity of profile temperature to the bag's stated brew temperature when available, else alphabetical). Tiers ① and ② SHALL render as tiles carrying real profile metadata (at minimum temperature and target yield) with the recommendation reason as a chip on the tile — never as detached right-aligned text; tier ③ SHALL remain a compact list. A search field SHALL always be available and SHALL filter within the drink type's set.

#### Scenario: Recently used profile ranks first
- **WHEN** the user picks a bean they have pulled shots with
- **THEN** the profile used most recently with that bean appears at the top under a "Used with this bean" header

#### Scenario: Reason rides its tile
- **WHEN** a KB-recommended profile appears in the recommended tier
- **THEN** its "suits <roast> roasts" reason renders as a chip on that profile's tile

#### Scenario: Tea type match recommended cold
- **WHEN** the user picks a tea bag whose extracted teaType is "black" and has no shot history with it
- **THEN** the stock black-tea profile ranks at the top with a label indicating the type match

#### Scenario: Missing beverage_type lands in espresso
- **WHEN** a community profile has no beverage_type
- **THEN** it appears in the espresso-family profile lists and not in filter or tea lists

### Requirement: Summary page is the edit surface
The wizard's final step SHALL be a summary whose hero is the recipe card rendered exactly as the management page renders it (same component: photo, drink-type icon + short label, profile, bag, plan line including milk weight) — what the user builds is what they will later see in the list. Below the hero: the recipe name field plus one row per component (drink type, bag, profile, details, steam/milk when present, hot water when present, equipment). Every stored block SHALL have a visible row — a latte's milk weight SHALL appear on the summary, not only behind an edit action. Each row SHALL use a single edit affordance glyph; the same glyph SHALL NOT be used with two meanings on one row (no dose→yield arrow beside a navigation arrow). Tapping a row SHALL open the corresponding step and return to the summary. Edit, clone, and promote-from-shot entry points SHALL open the wizard directly on the summary with all state loaded; for recipes without a stored drink type, the type SHALL be derived from the blocks and profile beverage type. Clone SHALL focus the name field for immediate rename and record clone provenance; promote-from-shot SHALL prefill from the shot record and its steam/hot-water snapshots (falling back to current settings when absent), recording shot provenance and the shot's bag. The summary SHALL retain add/remove affordances for the milk and hot-water blocks.

#### Scenario: Summary shows the future card
- **WHEN** the user reaches the summary for a latte with 200g milk
- **THEN** the hero card shows the drink icon, short type label, profile, bag, and a plan line including the milk weight

#### Scenario: Edit one field without a wizard walk
- **WHEN** the user edits an existing recipe and changes only the milk weight
- **THEN** they land on the summary, open the steam row, change the value, and save — never passing through the drink-type, bean, or profile steps

#### Scenario: Promote lands on summary
- **WHEN** the user promotes a shot with a hot-water snapshot
- **THEN** the summary shows all prefilled components including the derived drink type and the shot's bag, and saving creates the recipe with shot provenance

### Requirement: Name auto-suggestion from bean and drink type
The wizard SHALL suggest a recipe name composed from the bean (coffee/tea name) and the drink type's short label (e.g. "Ethiopia Latte" — never a label containing a slash or parenthetical), applied only while the name field is empty or still holds the previous suggestion — never over a user edit. When the bean name already ends with the drink-type word (case-insensitive), the suggestion SHALL NOT append it again.

#### Scenario: Suggestion follows selections
- **WHEN** the user picks a bean and drink type without typing a name
- **THEN** the name field shows the suggestion, and changing the bean updates it

#### Scenario: No stuttered type word
- **WHEN** the bean "Milk Blend Espresso" is picked for an espresso recipe
- **THEN** the suggestion is "Milk Blend Espresso", not "Milk Blend Espresso Espresso"

#### Scenario: Short label in names
- **WHEN** the user picks Latte/Cappuccino for the bean "Gran Bar"
- **THEN** the suggestion is "Gran Bar Latte", not "Gran Bar Latte / Cappuccino"

#### Scenario: User edit wins
- **WHEN** the user types their own name and then changes the profile
- **THEN** the typed name is unchanged

## ADDED Requirements

### Requirement: Details step fits one screen with right-sized controls
The details step's controls SHALL be sized to their content, not stretched to fill the row (a temperature stepper or a numeric field SHALL NOT span the page width). On landscape tablet layouts the section cards SHALL arrange in a multi-column grid so the step fits without scrolling for the common drink types. The grind knowledge-base hint (last grind for this bean, cross-profile direction) SHALL render as a visually anchored callout (icon plus distinct background), not as muted caption text.

#### Scenario: Latte details on a tablet
- **WHEN** the user reaches the details step for a latte on a landscape tablet
- **THEN** all sections (numbers, grind, steam, equipment) are visible without scrolling and no input control spans the full page width

#### Scenario: Grind hint is prominent
- **WHEN** a grind hint is available for the chosen bean and profile
- **THEN** it renders as a callout with an icon, visually distinct from field labels

### Requirement: Sub-pickers show preset metadata
The pitcher, water-vessel, and equipment picker dialogs SHALL show each entry's stored data on its row — pitcher: name with milk weight/temperature where stored; vessel: name with amount (per its mode) and temperature; equipment: package name with grinder and basket. Rows SHALL NOT be name-only.

#### Scenario: Vessel choice is informed
- **WHEN** the vessel picker opens with a "Mug" preset storing 220 ml at 96°C
- **THEN** the row reads the name plus "220ml · 96°C" rather than "Mug" alone
