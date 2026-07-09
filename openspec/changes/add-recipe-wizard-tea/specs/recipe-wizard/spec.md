# recipe-wizard Specification (delta)

## ADDED Requirements

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
The bean step SHALL list open bags whose kind matches the drink type (tea → tea bags; all other types → coffee bags) and SHALL offer an explicit "No bean" choice. Skipping SHALL produce a bean-less recipe per the optionality ladder (grind stored recipe-locally for coffee drinks).

#### Scenario: Tea drink shows only tea bags
- **WHEN** the user picks Tea and reaches the bean step
- **THEN** only bags with kind "tea" are listed

#### Scenario: No bean
- **WHEN** the user chooses "No bean"
- **THEN** the wizard advances to the profile step and the saved recipe has no bean link

### Requirement: Profile step filters by drink type and ranks by history
The profile step SHALL show only profiles whose `beverage_type` matches the drink type's filter set: espresso, americano, long black, and latte/cappuccino → `espresso` (a missing or empty `beverage_type` SHALL be treated as espresso); filter → `filter` and `pourover`; tea → `tea_portafilter`. Maintenance beverage types SHALL never appear. Profiles SHALL be presented in ranked tiers with visible headers: ① profiles used with this bean (exact bean identity match in shot history, most recent first), ② profiles used with similar beans (coffee: same roast level; tea: same tea type — where type-matches between the bag's tea type and stock tea profile names rank first with a reason label), ③ all remaining profiles in the filter set (coffee: alphabetical; tea: ordered by proximity of profile temperature to the bag's stated brew temperature when available, else alphabetical). A search field SHALL always be available and SHALL filter within the drink type's set.

#### Scenario: Recently used profile ranks first
- **WHEN** the user picks a bean they have pulled shots with
- **THEN** the profile used most recently with that bean appears at the top under a "Used with this bean" header

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
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean+profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data — temperature from `brewTempC`, dose computed from `leafGramsPer100Ml` and the target volume; (3) the profile's recommended dose, target weight, and temperature. For portafilter tea, the bag's `brewTempC` SHALL seed a temperature override only when the chosen profile is not type-matched to the bag's tea type; hot-water tea SHALL use the bag's brewing numbers verbatim. Prefilled values SHALL never overwrite a value the user has already edited in this wizard session.

#### Scenario: History beats profile defaults
- **WHEN** the user picks a bean+profile pair they have brewed before
- **THEN** dose/yield/temp/grind show the values from the most recent such shot, not the profile's recommendations

#### Scenario: Type-matched tea profile keeps its temperature
- **WHEN** a black tea bag stating 100°C is paired with the stock black-tea profile
- **THEN** no temperature override is seeded

#### Scenario: Generic tea profile gets corrected
- **WHEN** a sencha bag stating 70°C is paired with a generic tea profile at 94°C
- **THEN** the temperature field seeds 70°C as a recipe override

### Requirement: Drink-type-specific details fields
The details step SHALL show only the fields relevant to the drink type: espresso and filter — dose, yield, temperature, grind (inherit-or-override per the recipe model), equipment; americano and long black — the espresso fields plus the water-vessel picker with the order fixed by the type; latte/cappuccino — the espresso fields plus milk weight and pitcher; tea (portafilter) — leaf dose, yield, temperature, equipment, and NO grind fields; tea (hot water) — vessel, volume, temperature, leaf dose only.

#### Scenario: Tea hides grind
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** no grind or rpm fields are shown and the saved recipe stores no pinned grind

### Requirement: Summary page is the edit surface
The wizard's final step SHALL be a summary: the recipe name plus one row per component (drink type, bean, profile, details, equipment, blocks). Tapping a row SHALL open the corresponding step and return to the summary. Edit, clone, and promote-from-shot entry points SHALL open the wizard directly on the summary with all state loaded; for recipes without a stored drink type, the type SHALL be derived from the blocks and profile beverage type. Clone SHALL focus the name field for immediate rename and record clone provenance; promote-from-shot SHALL prefill from the shot record and its steam/hot-water snapshots (falling back to current settings when absent), recording shot provenance.

#### Scenario: Edit one field without a wizard walk
- **WHEN** the user edits an existing recipe and changes only the milk weight
- **THEN** they land on the summary, open the details row, change the value, and save — never passing through the drink-type, bean, or profile steps

#### Scenario: Promote lands on summary
- **WHEN** the user promotes a shot with a hot-water snapshot
- **THEN** the summary shows all prefilled components including the derived drink type, and saving creates the recipe with shot provenance

### Requirement: Per-drink-type equipment default
The equipment row SHALL prefill with the equipment package most recently used on a recipe of the same drink type; when none exists, the currently active package; when none, "none". The row SHALL be changeable from the details step and the summary.

#### Scenario: Tea remembers the tea setup
- **WHEN** the user creates a second tea recipe after setting a basket-only package on the first
- **THEN** the equipment row prefills with that package

### Requirement: Name auto-suggestion from bean and drink type
The wizard SHALL suggest a recipe name composed from the bean (coffee/tea name) and the drink type (e.g. "Ethiopia Latte"), applied only while the name field is empty or still holds the previous suggestion — never over a user edit.

#### Scenario: Suggestion follows selections
- **WHEN** the user picks a bean and drink type without typing a name
- **THEN** the name field shows the suggestion, and changing the bean updates it

#### Scenario: User edit wins
- **WHEN** the user types their own name and then changes the profile
- **THEN** the typed name is unchanged
