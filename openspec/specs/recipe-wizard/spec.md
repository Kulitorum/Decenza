# recipe-wizard Specification

## Purpose
Covers the recipe wizard that creates and edits drink recipes: the drink-type-first walk, the bean and profile pickers, how the details step prefills, and the summary page that is the edit surface for every recipe component.

## Requirements

### Requirement: Drink-type-first step sequence
The recipe wizard SHALL be the single surface for creating and editing recipes. Creation SHALL walk: drink type → bean → profile → equipment → dose/yield/temp/grind → steam and/or water (only the blocks the drink carries) → summary. Picker steps SHALL auto-advance on selection; post-profile windows SHALL be forms with an explicit Continue. Breadcrumb chips SHALL provide back-navigation.

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

#### Scenario: Drink types offered
- **WHEN** the user opens the recipe wizard
- **THEN** the drink types are espresso, filter, americano, long black, latte/cappuccino, latte + water, and tea

#### Scenario: Back arrow steps through windows in reverse
- **WHEN** the user taps the bottom-bar back arrow on a post-profile window
- **THEN** the previous window in the walk opens

#### Scenario: Edit reuses the same windows
- **WHEN** the user edits, clones or promotes a recipe
- **THEN** the wizard opens on the summary, and each card opens the same window the creation walk shows

### Requirement: Drink-type templates set defaults without restricting composition
Each drink type SHALL configure the wizard through a static template: the profile beverage-type filter, the bag kind filter, block pre-seeds, and the details-step field list. The stored blocks SHALL remain the sole source of truth for machine behavior. The summary SHALL offer add/remove affordances for the milk and hot-water blocks regardless of template, so every block combination the recipe model allows stays creatable.

#### Scenario: Latte template pre-seeds milk
- **WHEN** the user picks Latte/Cappuccino
- **THEN** the details step includes the milk fields and the saved recipe carries a steam block with hasMilk true

#### Scenario: Latte + Water template pre-seeds both blocks
- **WHEN** the user picks Latte + Water
- **THEN** the details walk includes both the milk fields and the water-vessel picker (with no before/after order choice), and the saved recipe carries a steam block with hasMilk true AND a hot-water block with hasWater true and order "before"

#### Scenario: Template escape hatch
- **WHEN** the user creates an Espresso recipe and, on the summary, adds the hot-water block
- **THEN** the recipe saves with both drink type "espresso" and a hot-water block, and activation behaves per the blocks

#### Scenario: Block pre-seeds per drink type
- **WHEN** the user picks Americano, Long Black, or Latte + Water
- **THEN** Americano pre-enables the hot-water block with order "after", Long Black with order "before", and Latte + Water pre-enables both the milk steam block and a hot-water block with order "before" and no order choice

### Requirement: Bean step filters by bag kind and is skippable
The bean step SHALL list open bags whose kind matches the drink type (tea → tea bags; other types → coffee bags) as a tile grid. Each open bag SHALL be its own tile; bags SHALL NOT be deduplicated by bean. "Add a new coffee…" (or tea) and "No bean" SHALL render as ghost tiles at the end of the grid. Selecting a tile SHALL link that specific bag.

#### Scenario: Two bags of the same bean are distinguishable
- **WHEN** two open bags of the same bean with different roast dates appear on the bean step
- **THEN** each shows its own tile with its roast date/age, and selecting one links exactly that bag

#### Scenario: Tea drink shows only tea bags
- **WHEN** the user picks Tea and reaches the bean step
- **THEN** only bags with kind "tea" are listed

#### Scenario: No bean
- **WHEN** the user chooses "No bean"
- **THEN** the wizard advances to the profile step and the saved recipe has no bag link

#### Scenario: Tile content
- **WHEN** the bean step lists an open bag
- **THEN** its tile shows the bag photo from the bean-image cache, the roaster as a caption, the coffee name, and the roast date or age
- **AND** ghost tiles use a dashed border at the same size as bag tiles

#### Scenario: Skipping the bean for a coffee drink
- **WHEN** the user chooses No bean for a coffee drink
- **THEN** the saved recipe has no bag link and stores its grind recipe-locally

### Requirement: Profile step filters by drink type and ranks by history
The profile step SHALL be the shared profile picker constrained by drink type: it SHALL list only profiles whose beverage_type is in the drink type's filter set, and SHALL NOT show its Beverage chip group while constrained. Maintenance beverage types SHALL never appear. A search field SHALL always be available and SHALL filter within the drink type's set.

#### Scenario: Recently used profile ranks first
- **WHEN** the user picks a bean they have pulled shots with
- **THEN** the profile used most recently with that bean appears first in the "Recommended for ‹bean›" row, labelled "used with ‹bean›"

#### Scenario: Reason rides its tile
- **WHEN** a KB-recommended profile appears in the recommended tier
- **THEN** its "suits <roast> roasts" reason renders as a chip on that profile's card

#### Scenario: Tea type match recommended cold
- **WHEN** the user picks a tea bag whose extracted teaType is "black" and has no shot history with it
- **THEN** the stock black-tea profile ranks at the top with a label indicating the type match

#### Scenario: Missing beverage_type lands in espresso
- **WHEN** a community profile has no beverage_type
- **THEN** it appears in the espresso-family profile lists and not in filter or tea lists

#### Scenario: Beverage chips hidden under a drink type
- **WHEN** the wizard's profile step opens for a tea drink
- **THEN** no Beverage chip group is shown and only tea profiles are listed, filterable by Selected, Favorites, Source and search

#### Scenario: Beverage filter sets
- **WHEN** the profile step opens for each drink type
- **THEN** espresso, americano, long black and latte/cappuccino list `espresso` profiles, filter lists `filter` and `pourover` profiles, and tea lists `tea_portafilter` profiles

#### Scenario: Other chips stay available
- **WHEN** the profile step is constrained by drink type
- **THEN** the Selected, Favorites and Source chips and the sort control remain available

### Requirement: Recommended profile row ranks by history and knowledge
Profiles SHALL be presented as one "Recommended for ‹bean›" row above the grid, ranked: ① profiles used with this bean, most recent first; ② knowledge-driven and similar-bean recommendations. The recommended tier SHALL be capped at five; candidates beyond the cap SHALL fall through to ③ all remaining profiles in the filter set, ordered by the sort control.

#### Scenario: Coffee and tea recommendation order
- **WHEN** the user picks a coffee bag with a roast-affinity knowledge-base entry, or a tea bag with a known tea type
- **THEN** coffee lists affinity profiles first with a "suits <roast> roasts" reason, then profiles used with same-roast beans; tea lists type-matched stock profiles first, then same-tea-type history

### Requirement: Profile cards carry metadata and affordances
Every tier SHALL render as the picker's cards, each with real profile metadata (at least temperature and target yield) from the catalog cache, without per-tile file reads. Recommendation reasons SHALL ride as a chip on the card, never as detached text. Each card SHALL offer the knowledge-base popup, Profile Info, the favorite star and the ⋮ actions dialog without selecting the profile.

#### Scenario: Card actions without selecting
- **WHEN** the user taps the (i) button on a profile card without selecting it
- **THEN** the Profile Info page opens and the profile stays unselected

### Requirement: Tea profile step offers "Just hot water"
For the tea drink type, the profile step SHALL include a fixed "Just hot water" card (below the ranked profiles and the grid, visible regardless of search text or chips). Selecting it SHALL produce a profile-less recipe whose drink type is hot-water tea, and the details step SHALL show only vessel, volume, temperature, and optional leaf dose.

#### Scenario: Hot-water tea recipe
- **WHEN** the user picks Tea, a tea bag, then "Just hot water"
- **THEN** the details step shows vessel/volume/temperature/leaf dose and saving succeeds with no profile

#### Scenario: Visible under filters
- **WHEN** the user has Favorites on and a search text that matches nothing
- **THEN** the "Just hot water" card is still shown

### Requirement: Details step prefills from history, then bag data, then profile defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean and profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data; (3) the profile's recommended dose, target weight, and temperature. Prefilled values SHALL never overwrite a value the user has edited in this wizard session.

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

#### Scenario: No shot history falls back to the bag's current dial
- **WHEN** the user creates a recipe for a bean+profile pair with no prior shot history, and the linked bag's current grind is "18"
- **THEN** the grind field prefills "18" as a one-time default, not a live-following value

#### Scenario: Hot-water tea uses bag brewing numbers
- **WHEN** the user creates a hot-water tea recipe from a bag stating brewTempC and leafGramsPer100Ml
- **THEN** the fields use the bag's brewing numbers verbatim, with the leaf dose computed from leafGramsPer100Ml and the target volume

#### Scenario: Edited field is not overwritten
- **WHEN** the user has edited the dose and a prefill tier would otherwise seed it
- **THEN** the user's dose is kept

### Requirement: Grind hint names the last grind for the bean
For coffee drinks the grind section SHALL show a hint: the latest grind dialed for this bean regardless of profile, falling back to same-roast-level beans, naming its profile. The hint SHALL NEVER present a computed grinder number for a different profile; it SHALL state the relative direction ("finer"/"coarser") only when both profiles have known UGS positions.

#### Scenario: Grind hint from a same-roast bean
- **WHEN** no shot with this bean has a grind but a same-roast bean does
- **THEN** the hint names that bean's last grind and its profile

### Requirement: No-history grind default comes from the bag once
With no matching shot history for the chosen bean and profile, the grind and rpm fields SHALL fall back to the linked bag's current grinderSetting and rpm as a one-time editable default, offered rather than silently applied. With no linked bag and no history, the fields SHALL start empty.

#### Scenario: Default is editable before saving
- **WHEN** the user changes the prefilled grind before saving
- **THEN** the recipe saves with the changed grind

### Requirement: Drink-type-specific details fields
The details step SHALL show only the fields relevant to the drink type. Espresso and filter show dose, yield, temperature, grind (recipe-owned, with no inherit/override toggle) and equipment; americano and long black add the water-vessel picker with order fixed by type; latte/cappuccino add milk weight and pitcher.

#### Scenario: Tea hides grind
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** no grind or rpm fields are shown and the saved recipe stores no pinned grind

#### Scenario: Tea field sets
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** it shows leaf dose, yield, temperature and equipment, with no grind or rpm fields
- **AND** a hot-water tea recipe shows only vessel, volume, temperature and leaf dose

### Requirement: Summary page is the edit surface
The wizard's final step SHALL be a summary whose hero is the recipe card rendered by the management page's own component. The recipe name with Cancel/Save and any save error SHALL sit in a header pinned above the scrolling body. Each component SHALL render as a tappable card that opens its own window and returns to the summary.

#### Scenario: Summary shows the future card
- **WHEN** the user reaches the summary for a latte with 200g milk
- **THEN** the hero card shows the drink icon, short type label, profile, bag, and a plan line including the milk weight

#### Scenario: Edit one field without a wizard walk
- **WHEN** the user edits an existing recipe and changes only the milk weight
- **THEN** they land on the summary, open the steam row, change the value, and save — never passing through the drink-type, bean, or profile steps

#### Scenario: Promote lands on summary
- **WHEN** the user promotes a shot with a hot-water snapshot
- **THEN** the summary shows all prefilled components including the derived drink type and the shot's bag, and saving creates the recipe with shot provenance

#### Scenario: Details card shows all pinned values, not just dose→yield
- **WHEN** an espresso recipe pins 18.0g dose, a 1:2.0 ratio yielding 36.0g, 94°C, and grind 8
- **THEN** the Dose/yield/temp/grind card shows the dose, the ratio and resulting yield (e.g. "1:2.0 → 36.0g"), the temperature, and the grind — not only "18.0g → 36.0g"

#### Scenario: Ratio yield is shown as a ratio
- **WHEN** a recipe expresses yield as a ratio rather than a fixed weight
- **THEN** the Dose/yield/temp/grind card labels it as a ratio (with the resulting weight), rather than presenting only a fixed weight

#### Scenario: rpm appears only for RPM-controlled grinders
- **WHEN** the recipe's equipment uses an RPM-controlled grinder and the recipe pins an rpm
- **THEN** the Dose/yield/temp/grind card shows the rpm alongside the grind; and when the grinder is not RPM-controlled, no rpm is shown

#### Scenario: Tea details omit grind
- **WHEN** the recipe is a portafilter tea recipe
- **THEN** the Dose/yield/temp/grind card shows leaf dose, yield, and temperature and shows no grind or rpm

#### Scenario: Bean card shows photo and detail
- **WHEN** a recipe is linked to a bag that has a cached photo and a known roaster and roast date
- **THEN** the Bean card shows the photo, the roaster, the coffee name, and the roast date or age

#### Scenario: Profile card is a rich, non-duplicating read-out
- **WHEN** a recipe overrides the profile's temperature and dose and the profile is a D-Flow profile
- **THEN** the Profile card shows the profile name, its editor/type classification ("D-Flow"), its beverage type, and a substantive pressure/flow shape summary — and does NOT restate the recipe's temperature or dose (those appear only on the Dose/yield/temp/grind card)

#### Scenario: Profile Info and knowledge-base buttons are reachable from the summary
- **WHEN** the profile on the summary has a knowledge-base entry
- **THEN** the Profile card shows the "(i)" Profile Info button and the sparkle knowledge-base button, each opening its respective view without leaving the summary, and both are visually distinct from the card's edit glyph

#### Scenario: Knowledge-base button hidden when no KB entry
- **WHEN** the profile has no knowledge-base entry
- **THEN** the sparkle knowledge-base button is not shown, while the "(i)" Profile Info button remains available

#### Scenario: Equipment card excludes grind and rpm
- **WHEN** the recipe has an equipment package that includes a grinder plus its grind setting and rpm
- **THEN** the Equipment card lists the package's equipment (grinder model, basket, puck-prep, etc.) but does NOT show the grind setting or rpm, which appear only on the Dose/yield/temp/grind card

#### Scenario: Equipment card sits above the numbers card
- **WHEN** the user views the summary of a coffee recipe
- **THEN** the Equipment card appears above the Dose/yield/temp/grind card, matching the create-walk order

#### Scenario: Each card opens its own window, same for edit and create
- **WHEN** the user taps the Equipment card on the summary
- **THEN** the equipment window opens (the same screen the creation walk shows), and Done returns to the summary; tapping the Dose/yield/temp/grind, Steam, or Hot water card likewise opens exactly that window

#### Scenario: Equipment window offers inline tiles
- **WHEN** the user opens the equipment window
- **THEN** the in-inventory packages are shown as inline tap-to-select tiles with the linked one highlighted (plus a "None" tile), and selecting one links it without opening a separate dialog

#### Scenario: Every stored block has a card
- **WHEN** the recipe stores a steam or hot-water block
- **THEN** a visible card for it appears on the summary, not only behind an edit action

#### Scenario: One glyph per card
- **WHEN** the Dose/yield/temp/grind card renders
- **THEN** it shows a single edit glyph and no dose→yield arrow

### Requirement: Edit, clone and promote open the summary with state loaded
Edit, clone and promote-from-shot SHALL open the wizard on the summary with all state loaded. A recipe without a stored drink type SHALL derive it from its blocks and profile beverage type. Clone SHALL focus the name field and record clone provenance. Promote SHALL prefill from the shot record and its steam and hot-water snapshots, falling back to current settings, and record shot provenance.

#### Scenario: Clone focuses the name
- **WHEN** the user clones a recipe
- **THEN** the name field is focused for immediate rename and the clone provenance is recorded

### Requirement: Component value summaries show every value their editor changes
Each component card's value summary SHALL present the full set of values its own editor changes, scoped to the fields the drink type has, and no value SHALL be repeated across cards. Summaries SHALL be internationalized and SHALL omit absent fields rather than show empty or placeholder values.

#### Scenario: Coffee dose card values
- **WHEN** a coffee recipe's Dose/yield/temp/grind card renders
- **THEN** it shows dose, yield mode with the resulting weight, effective temperature and grind under a title naming grind, and rpm only when the grinder is RPM-controlled

#### Scenario: Hot-water tea summary
- **WHEN** a hot-water tea recipe's numbers card renders
- **THEN** it shows volume and temperature only, with no dose or grind

### Requirement: Bean card shows photo and bag detail
The Bean card SHALL show the linked bag's cached photo, rendered without a colour emoji in plain Text, with the roaster, coffee name, and roast level and/or roast date or age, matching the bean step tile. A bag-less recipe SHALL render a clear "No bean" state.

#### Scenario: Bag-less recipe shows No bean
- **WHEN** the recipe has no linked bag
- **THEN** the Bean card shows the "No bean" state

### Requirement: Profile card is a rich read-out
The Profile card SHALL show the profile name, its editor/type classification, its beverage type, and a substantive summary of its pressure/flow shape, EXCLUDING parameters the recipe overrides. It SHALL expose the Profile Info (i) button and, when the profile has a knowledge-base entry, the sparkle AI DB button, both distinct from the edit glyph.

#### Scenario: Profile-less recipe
- **WHEN** the recipe has no profile (hot-water tea)
- **THEN** the Profile card shows a "No profile" hot-water state

### Requirement: Steam card shows a real summary
When a steam block is present, the steam card SHALL show a real summary: milk weight and pitcher, and the block's steam target and settings where set, not a bare title.

#### Scenario: Steam card detail
- **WHEN** a recipe has a steam block with a milk weight and pitcher
- **THEN** the steam card shows both values

### Requirement: Equipment card lists the package
The Equipment card SHALL show the full equipment package EXCLUDING grind setting and rpm, which are recipe-owned and shown on the Dose card. With no package it SHALL render "none". The equipment window SHALL present in-inventory packages as inline tap-to-select tiles plus a "None" tile, highlighting the linked one.

#### Scenario: No package shows none
- **WHEN** the recipe has no equipment package
- **THEN** the Equipment card renders "none"

### Requirement: Per-drink-type equipment default
The equipment row SHALL prefill with the equipment package most recently used on a recipe of the same drink type; when none exists, the currently active package; when none, "none". The row SHALL be changeable from the details step and the summary.

#### Scenario: Tea remembers the tea setup
- **WHEN** the user creates a second tea recipe after setting a basket-only package on the first
- **THEN** the equipment row prefills with that package

### Requirement: Name auto-suggestion from bean and drink type
The wizard SHALL suggest a recipe name composed from the bean, the drink type's short label, and the profile (e.g. "Yirgacheffe Latte · Cremina"), never containing a slash or parenthetical. The suggestion SHALL apply only while the name field is empty or still holds the previous suggestion, and SHALL update when the bean, drink type, or profile changes.

#### Scenario: Suggestion follows selections
- **WHEN** the user picks a bean and drink type without typing a name
- **THEN** the name field shows the suggestion, and changing the bean, drink type, or profile updates it

#### Scenario: Profile is included from the first recipe
- **WHEN** the user builds a Latte for the bean "Yirgacheffe" on the profile "Cremina"
- **THEN** the suggestion is "Yirgacheffe Latte · Cremina"

#### Scenario: Editor prefix is stripped
- **WHEN** the selected profile's title is "D-Flow/Extractamundo"
- **THEN** the appended profile token is "Extractamundo", not "D-Flow/Extractamundo"

#### Scenario: No stuttered profile word
- **WHEN** an espresso recipe uses the profile "Blooming Espresso"
- **THEN** the suggestion does not repeat "Espresso" after the profile (no "… Espresso · Blooming Espresso")

#### Scenario: No stuttered type word
- **WHEN** the bean "Milk Blend Espresso" is picked for an espresso recipe
- **THEN** the suggestion is "Milk Blend Espresso", not "Milk Blend Espresso Espresso" (before any profile token)

#### Scenario: Short label in names
- **WHEN** the user picks Latte/Cappuccino for the bean "Gran Bar"
- **THEN** the suggestion uses "Latte", not "Latte / Cappuccino"

#### Scenario: Hot-water tea has no profile token
- **WHEN** the user builds a "Just hot water" tea recipe (no profile)
- **THEN** the suggestion is composed from the bean and drink type only, with no profile token

#### Scenario: Collision falls to a dial-in qualifier
- **WHEN** a recipe named "Yirgacheffe Espresso · Cremina" already exists and the user builds another whose composed name matches it
- **THEN** the suggestion appends the draft's own yield (ratio or target weight), else its dose, not a numeric counter

#### Scenario: User edit wins
- **WHEN** the user types their own name and then changes the profile
- **THEN** the typed name is unchanged

### Requirement: Profile token is cleaned before use
The profile token SHALL have any `D-Flow/` or `A-Flow/` editor-membership prefix removed. It SHALL NOT be appended when its trailing word repeats the drink-type word (case-insensitive), and SHALL NOT be appended when no profile is selected.

#### Scenario: Editor prefix stripped and stutter avoided
- **WHEN** an espresso recipe uses the profile "D-Flow/Blooming Espresso"
- **THEN** no profile token is appended, because its trailing word repeats "Espresso"

### Requirement: Name collisions take a dial-in qualifier
When the composed name matches an existing non-archived recipe's display name (case-insensitive), the wizard SHALL append a qualifier from the draft's own yield (ratio or target weight), else its dose, retrying against the name set. The wizard SHALL NOT use a bare numeric counter.

#### Scenario: Yield collision falls to dose
- **WHEN** the yield-qualified name also exists
- **THEN** the wizard appends the draft's dose as the qualifier

### Requirement: Details step fits one screen with right-sized controls
The details step's controls SHALL be sized to their content, not stretched to fill the row (a temperature stepper or a numeric field SHALL NOT span the page width). On landscape tablet layouts the section cards SHALL arrange in a multi-column grid so the step fits without scrolling for the common drink types. The grind knowledge-base hint (last grind for this bean, cross-profile direction) SHALL render as a visually anchored callout (icon plus distinct background), not as muted caption text.

#### Scenario: Latte details on a tablet
- **WHEN** the user reaches the details step for a latte on a landscape tablet
- **THEN** all sections (numbers, grind, steam, equipment) are visible without scrolling and no input control spans the full page width

#### Scenario: Grind hint is prominent
- **WHEN** a grind hint is available for the chosen bean and profile
- **THEN** it renders as a callout with an icon, visually distinct from field labels

### Requirement: Details step explains its prefills and reads as optional
The details step SHALL present itself as optional, with a caption stating everything is prefilled and ready to save. The numbers and grind cards SHALL open collapsed to a one-line summary, and SHALL auto-expand only when nothing could be prefilled. The steam card SHALL NOT capture a milk weight: the recipe stores the pitcher and milk intent.

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

#### Scenario: Expanded cards explain their values
- **WHEN** the user expands the numbers card
- **THEN** its caption names the provenance tier and suggests adjusting to taste, including what the temperature offset means
- **AND** the grind card explains the inherit-vs-override rule, and the equipment card says it was prefilled from the last use for this drink type

#### Scenario: Stored milk weight still displays
- **WHEN** a recipe with a stored milk weight, such as one promoted from a shot's steam snapshot, is shown
- **THEN** the weight displays on the cards and the summary

### Requirement: Sub-pickers show preset metadata
The pitcher, water-vessel, and equipment picker dialogs SHALL show each entry's stored data on its row — pitcher: name with milk weight/temperature where stored; vessel: name with amount (per its mode) and temperature; equipment: package name with grinder and basket. Rows SHALL NOT be name-only.

#### Scenario: Vessel choice is informed
- **WHEN** the vessel picker opens with a "Mug" preset storing 220 ml at 96°C
- **THEN** the row reads the name plus "220ml · 96°C" rather than "Mug" alone

### Requirement: Equipment precedes grind in the details layout
The details step SHALL present the equipment selection before (above) the grind/rpm fields, so the recipe's grinder rpm-capability is known — and the rpm field's visibility is correct — the first time the user reaches the grind fields, rather than only after separately visiting the equipment section further down the step.

#### Scenario: Rpm field is correct on first view
- **WHEN** the user reaches the details step for a drink type with no per-drink-type equipment default yet, and their only/selected grinder is rpm-capable
- **THEN** the equipment section (already resolved or explicitly chosen) appears before the grind fields, and the rpm field is visible when the user reaches it — not hidden until they scroll past the grind card to equipment

#### Scenario: Changing equipment after grind entry updates rpm visibility immediately
- **WHEN** the user has already entered a grind value and then changes the equipment selection to a non-rpm-capable grinder
- **THEN** the rpm field hides immediately, consistent with the newly selected equipment

### Requirement: The equipment window SHALL never open empty and SHALL be skipped when only one package exists

During the creation walk the equipment window SHALL preselect the currently active package rather than open with nothing chosen. When exactly one in-inventory package exists, the wizard SHALL select it and skip the window, advancing to the dose/yield/temp/grind window. Edit and clone flows keep the recipe's own package, and a summary-card jump always shows the window.

#### Scenario: Single package skips the window

- **GIVEN** exactly one equipment package in inventory
- **WHEN** the creation walk reaches the details step
- **THEN** the wizard SHALL select that package and open directly on the dose/yield/temp/grind window

#### Scenario: Multiple packages preselect the active one

- **GIVEN** several packages with one active
- **WHEN** the equipment window opens in the creation walk with no package chosen yet
- **THEN** the active package SHALL be preselected, and the window SHALL still be shown

#### Scenario: Web recipe creation links the active package

- **WHEN** a recipe is created from the `/recipes` web form
- **THEN** it SHALL be linked to the active equipment package
- **AND** its grind candidates SHALL resolve against that package's grinder

### Requirement: Temperature offset control shows the resulting temperature

On the coffee/espresso details step, the resulting brew temperature of the offset SHALL be shown adjacent to the offset control. It SHALL use the same formatter as brew-settings Temp Delta against the selected profile's own frame temperatures, never the active profile, and SHALL be unit-aware and visible only when that temperature is resolvable.

#### Scenario: Zero offset shows the profile temperature

- **WHEN** the details step is shown for a coffee drink whose profile brews at 94 °C and the temperature offset is 0°
- **THEN** the resulting-temperature readout shows the profile's temperature (e.g. "→ 94°C") with no offset tag

#### Scenario: Adjusted offset shows the resulting temperature and the tag

- **WHEN** the user sets the temperature offset to +2° on a 94 °C profile
- **THEN** the readout shows the resulting temperature with a signed offset tag (e.g. "→ 96°C +2°")

#### Scenario: Multi-temperature profile collapses to two readings

- **WHEN** the selected profile's frames use three or more distinct temperatures and an offset is applied
- **THEN** the readout shows only the shifted first and last temperatures joined by an ellipsis (e.g. "→ 88…94°C"), matching how brew settings renders the same profile

#### Scenario: Fahrenheit unit

- **WHEN** the temperature unit is set to Fahrenheit
- **THEN** the readout shows the resulting temperature and offset in °F, and re-renders when the unit is switched

#### Scenario: Unresolvable profile temperature

- **WHEN** the selected profile's temperature cannot be resolved
- **THEN** the resulting-temperature readout is hidden (the offset control is already disabled in this state)

#### Scenario: Tea has no readout
- **WHEN** the user edits a tea recipe's temperature
- **THEN** no resulting-temperature readout is shown, because tea sets an absolute temperature

#### Scenario: Readout is static text
- **WHEN** an assistive technology reads the resulting-temperature readout
- **THEN** it is exposed as static text

### Requirement: The pitcher picker offers "Heater off"

The steam pitcher picker SHALL offer the built-in "Heater off" entry alongside real presets, SHALL NOT filter it out, and SHALL NOT offer a way to create one. Choosing it SHALL store the off marker on the steam block rather than a pitcher name, and the steam and summary cards SHALL show it as the chosen entry.

#### Scenario: Heater off is selectable
- **WHEN** the user opens the pitcher picker while composing or editing a recipe
- **THEN** the built-in "Heater off" entry is offered and can be selected

#### Scenario: Choosing it stores the marker
- **WHEN** the user selects "Heater off" and saves the recipe
- **THEN** the recipe's steam block carries the off marker and no pitcher name

#### Scenario: The wizard cannot create one
- **WHEN** the user adds a new pitcher preset from the wizard or the Steam page
- **THEN** no heater-off option is offered

#### Scenario: A recipe carrying the marker displays it
- **WHEN** a recipe carrying the off marker is opened in the wizard
- **THEN** the steam card names the "Heater off" entry as the selection, not a blank pitcher
