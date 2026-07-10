# recipe-wizard Specification

## Purpose
TBD - created by archiving change add-recipe-wizard-tea. Update Purpose after archive.
## Requirements
### Requirement: Drink-type-first step sequence
The system SHALL provide a recipe wizard as the single surface for recipe creation and editing. Creation SHALL walk: drink type → bean → profile → details → summary. The drink types SHALL be espresso, filter, americano, long black, latte/cappuccino, and tea. Picker steps (drink type, bean, profile) SHALL auto-advance on selection with no Next button; the details step SHALL be a form with an explicit continue. Breadcrumb chips showing selections so far SHALL provide back-navigation to any earlier step.

#### Scenario: Three taps to the details step
- **WHEN** the user taps Espresso, then a bag, then a profile
- **THEN** the wizard is on the details step with no intermediate Next taps

#### Scenario: Breadcrumb returns to an earlier step
- **WHEN** the user taps the bean chip while on the details step
- **THEN** the bean step reopens, and a new selection returns to where they were with dependent state updated

### Requirement: Drink-type templates set defaults without restricting composition
Each drink type SHALL configure the wizard via a static template: the profile beverage-type filter set, the bag kind filter, block pre-seeds (latte/cappuccino pre-enables the steam block with milk; americano pre-enables the hot-water block with order "after"; long black with order "before"), and the details-step field list. The stored blocks SHALL remain the sole source of truth for machine behavior. The summary SHALL offer add/remove affordances for the milk and hot-water blocks regardless of template, so any block combination expressible in the recipe model remains creatable.

#### Scenario: Latte template pre-seeds milk
- **WHEN** the user picks Latte/Cappuccino
- **THEN** the details step includes the milk fields and the saved recipe carries a steam block with hasMilk true

#### Scenario: Template escape hatch
- **WHEN** the user creates an Espresso recipe and, on the summary, adds the hot-water block
- **THEN** the recipe saves with both drink type "espresso" and a hot-water block, and activation behaves per the blocks

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
The profile step SHALL show only profiles whose `beverage_type` matches the drink type's filter set: espresso, americano, long black, and latte/cappuccino → `espresso` (a missing or empty `beverage_type` SHALL be treated as espresso); filter → `filter` and `pourover`; tea → `tea_portafilter`. Maintenance beverage types SHALL never appear. Profiles SHALL be presented in ranked tiers with visible headers: ① profiles used with this bean (exact bean identity match in shot history, most recent first), ② knowledge-driven recommendations and similar beans — coffee: profiles whose knowledge-base entry states an affinity for the bag's roast level (KB `roastAffinity`, authored only from each profile's own dial-in documentation, shown with a "suits <roast> roasts" reason label) rank first, then profiles used with same-roast-level beans; tea: type-matches between the bag's tea type and stock tea profile names rank first with a reason label, then same-tea-type history. The recommended tier SHALL be capped to a handful (5); candidates beyond the cap fall through to the final tier — ③ all remaining profiles in the filter set (coffee: alphabetical; tea: ordered by proximity of profile temperature to the bag's stated brew temperature when available, else alphabetical). ALL tiers SHALL render as tiles in the wizard's shared tile-grid language, each carrying real profile metadata (at minimum temperature and target yield, sourced from the profile catalog cache — no per-tile file reads); tiers ① and ② additionally carry the recommendation reason as a chip on the tile — never as detached right-aligned text. Each tile SHALL offer the same two info affordances as the profile selector page: the knowledge-base popup (sparkle icon, shown when the profile has a KB entry) and the Profile Info page (the (i) button) — both usable without selecting the profile. A search field SHALL always be available and SHALL filter within the drink type's set.

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

### Requirement: Tea profile step offers "Just hot water"
For the tea drink type, the profile step SHALL include a fixed "Just hot water" row (below the ranked profiles, visible regardless of search text). Selecting it SHALL produce a profile-less recipe whose drink type is hot-water tea, and the details step SHALL show only vessel, volume, temperature, and optional leaf dose.

#### Scenario: Hot-water tea recipe
- **WHEN** the user picks Tea, a tea bag, then "Just hot water"
- **THEN** the details step shows vessel/volume/temperature/leaf dose and saving succeeds with no profile

### Requirement: Details step prefills from history, then bag data, then profile defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean+profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data — temperature from `brewTempC`, dose computed from `leafGramsPer100Ml` and the target volume; (3) the profile's recommended dose, target weight, and temperature. For coffee drinks the grind section SHALL additionally show a grind hint: the latest grind dialed for this bean regardless of profile (falling back to same-roast-level beans), naming the profile it was dialed for, plus — when that profile differs from the picked one and both have known UGS positions — the relative direction ("finer"/"coarser") per the knowledge base's UGS ordering. The hint SHALL never present a computed grinder number for a different profile (the KB's own cross-profile rule: only direction translates). For portafilter tea, the bag's `brewTempC` SHALL seed a temperature override only when the chosen profile is not type-matched to the bag's tea type; hot-water tea SHALL use the bag's brewing numbers verbatim. Prefilled values SHALL never overwrite a value the user has already edited in this wizard session.

#### Scenario: History beats profile defaults
- **WHEN** the user picks a bean+profile pair they have brewed before
- **THEN** dose/yield/temp/grind show the values from the most recent such shot, not the profile's recommendations

#### Scenario: Type-matched tea profile keeps its temperature
- **WHEN** a black tea bag stating 100°C is paired with the stock black-tea profile
- **THEN** no temperature override is seeded

#### Scenario: Generic tea profile gets corrected
- **WHEN** a sencha bag stating 70°C is paired with a generic tea profile at 94°C
- **THEN** the temperature field seeds 70°C as a recipe override

#### Scenario: Grind hint translates direction across profiles
- **WHEN** the bean's last grind was 15 dialed for D-Flow and the user picked Rao Allongé
- **THEN** the grind section shows the 15 (naming D-Flow) and that Allongé typically grinds coarser — no computed number for Allongé

### Requirement: Drink-type-specific details fields
The details step SHALL show only the fields relevant to the drink type: espresso and filter — dose, yield, temperature, grind (inherit-or-override per the recipe model), equipment; americano and long black — the espresso fields plus the water-vessel picker with the order fixed by the type; latte/cappuccino — the espresso fields plus milk weight and pitcher; tea (portafilter) — leaf dose, yield, temperature, equipment, and NO grind fields; tea (hot water) — vessel, volume, temperature, leaf dose only.

#### Scenario: Tea hides grind
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** no grind or rpm fields are shown and the saved recipe stores no pinned grind

### Requirement: Summary page is the edit surface
The wizard's final step SHALL be a summary whose hero is the recipe card rendered exactly as the management page renders it (same component: photo, drink-type icon + short label, profile, bag, plan line including milk weight) — what the user builds is what they will later see in the list. The recipe name field with Cancel/Save (and any save error) SHALL sit in a header pinned above the scrolling body. Below the hero, each component (drink type, bag, profile, details, steam/milk when present, hot water when present, equipment) SHALL render as a tappable CARD — title, value summary, single edit glyph — in the same responsive card grid the details step uses. Every stored block SHALL have a visible card — a latte's milk weight SHALL appear on the summary, not only behind an edit action. The edit glyph SHALL NOT be used with two meanings on one card (no dose→yield arrow beside a navigation arrow). Tapping a card SHALL open the corresponding step and return to the summary. Edit, clone, and promote-from-shot entry points SHALL open the wizard directly on the summary with all state loaded; for recipes without a stored drink type, the type SHALL be derived from the blocks and profile beverage type. Clone SHALL focus the name field for immediate rename and record clone provenance; promote-from-shot SHALL prefill from the shot record and its steam/hot-water snapshots (falling back to current settings when absent), recording shot provenance and the shot's bag. The summary SHALL retain add/remove affordances for the milk and hot-water blocks.

#### Scenario: Summary shows the future card
- **WHEN** the user reaches the summary for a latte with 200g milk
- **THEN** the hero card shows the drink icon, short type label, profile, bag, and a plan line including the milk weight

#### Scenario: Edit one field without a wizard walk
- **WHEN** the user edits an existing recipe and changes only the milk weight
- **THEN** they land on the summary, open the steam row, change the value, and save — never passing through the drink-type, bean, or profile steps

#### Scenario: Promote lands on summary
- **WHEN** the user promotes a shot with a hot-water snapshot
- **THEN** the summary shows all prefilled components including the derived drink type and the shot's bag, and saving creates the recipe with shot provenance

### Requirement: Per-drink-type equipment default
The equipment row SHALL prefill with the equipment package most recently used on a recipe of the same drink type; when none exists, the currently active package; when none, "none". The row SHALL be changeable from the details step and the summary.

#### Scenario: Tea remembers the tea setup
- **WHEN** the user creates a second tea recipe after setting a basket-only package on the first
- **THEN** the equipment row prefills with that package

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

### Requirement: Details step fits one screen with right-sized controls
The details step's controls SHALL be sized to their content, not stretched to fill the row (a temperature stepper or a numeric field SHALL NOT span the page width). On landscape tablet layouts the section cards SHALL arrange in a multi-column grid so the step fits without scrolling for the common drink types. The grind knowledge-base hint (last grind for this bean, cross-profile direction) SHALL render as a visually anchored callout (icon plus distinct background), not as muted caption text.

#### Scenario: Latte details on a tablet
- **WHEN** the user reaches the details step for a latte on a landscape tablet
- **THEN** all sections (numbers, grind, steam, equipment) are visible without scrolling and no input control spans the full page width

#### Scenario: Grind hint is prominent
- **WHEN** a grind hint is available for the chosen bean and profile
- **THEN** it renders as a callout with an icon, visually distinct from field labels

### Requirement: Details step explains its prefills and reads as optional
The details step SHALL present itself as optional: a step-level caption SHALL state that everything is prefilled and ready to save. The numbers and grind cards SHALL open COLLAPSED to a one-line summary of their current values (tap to expand and edit); they SHALL auto-expand only when nothing could be prefilled (no dose/yield from any tier, or no bag to inherit grind from). When expanded, the numbers card SHALL carry a caption naming the provenance tier that filled it (last shot with this bean+profile / the profile's recommended numbers / the tea bag's brewing instructions / the recipe's saved values) plus a short adjust-to-taste nudge (including what the temp offset means); the grind card SHALL explain the inherit-vs-override rule; the equipment card SHALL say the package was prefilled from the user's last use for this drink type. The steam card SHALL NOT capture a milk weight — milk is weighed each time the user steams; the recipe stores the pitcher (whose preset carries steam time/flow/temperature) and the milk intent that drives the heater hold. A milk weight already stored on a recipe (e.g. promoted from a shot's steam snapshot) SHALL still display on cards and the summary.

#### Scenario: Prefilled step reads as done
- **WHEN** the user reaches the details step for a latte with history prefills
- **THEN** the numbers and grind cards show collapsed value summaries under an "everything here is optional" caption, and Continue proceeds without touching anything

#### Scenario: History prefill is named
- **WHEN** the user expands the numbers card after a history prefill
- **THEN** its caption says the values come from the last shot with these beans and this profile and invites adjusting them deliberately

#### Scenario: Blank state still guides
- **WHEN** no dose or yield could be prefilled from any tier
- **THEN** the numbers card opens expanded so the step is not a dead end

#### Scenario: No milk weight field
- **WHEN** the user reaches the details step for a latte
- **THEN** the steam card offers the pitcher picker and explains milk is weighed at steam time — there is no milk-weight input

### Requirement: Sub-pickers show preset metadata
The pitcher, water-vessel, and equipment picker dialogs SHALL show each entry's stored data on its row — pitcher: name with milk weight/temperature where stored; vessel: name with amount (per its mode) and temperature; equipment: package name with grinder and basket. Rows SHALL NOT be name-only.

#### Scenario: Vessel choice is informed
- **WHEN** the vessel picker opens with a "Mug" preset storing 220 ml at 96°C
- **THEN** the row reads the name plus "220ml · 96°C" rather than "Mug" alone

