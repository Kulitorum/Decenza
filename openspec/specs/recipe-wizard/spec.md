# recipe-wizard Specification

## Purpose
TBD - created by archiving change add-recipe-wizard-tea. Update Purpose after archive.
## Requirements
### Requirement: Drink-type-first step sequence
The system SHALL provide a recipe wizard as the single surface for recipe creation and editing. Creation SHALL walk: drink type → bean → profile → **equipment → dose/yield/temp/grind → steam and/or water (only the blocks the drink carries)** → summary. The drink types SHALL be espresso, filter, americano, long black, latte/cappuccino, and tea. Picker steps (drink type, bean, profile) SHALL auto-advance on selection with no Next button; the post-profile windows SHALL be forms with an explicit Continue (the last one reads "Review" and leads to the summary). The **equipment window SHALL come first among the post-profile windows**, so the grinder — whose RPM capability gates the rpm field — is chosen before the dose/yield/temp/grind window. The post-profile windows SHALL be the SAME screens used when editing: creation walks them in order, while edit/clone/promote open on the summary and jump straight to a window from the tapped card, returning to the summary. Breadcrumb chips showing the drink/bean/profile selections so far SHALL provide back-navigation; the bottom-bar back arrow SHALL step back through the windows in reverse.

#### Scenario: Equipment window comes before the numbers window
- **WHEN** the user taps Espresso, then a bag, then a profile
- **THEN** the wizard is on the equipment window, and continuing from it reaches the dose/yield/temp/grind window — the grinder is chosen before grind/rpm

#### Scenario: Only the blocks the drink has appear as windows
- **WHEN** the user creates a latte (milk, no water)
- **THEN** the walk includes a steam window and no water window; an americano's walk includes a water window and no steam window; a plain espresso's walk ends at the numbers window before the summary

#### Scenario: Breadcrumb returns to an earlier step
- **WHEN** the user taps the bean chip while on a post-profile window
- **THEN** the bean step reopens, and a new selection returns to the walk with dependent state updated

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
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean+profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data — temperature from `brewTempC`, dose computed from `leafGramsPer100Ml` and the target volume; (3) the profile's recommended dose, target weight, and temperature. For coffee drinks the grind section SHALL additionally show a grind hint: the latest grind dialed for this bean regardless of profile (falling back to same-roast-level beans), naming the profile it was dialed for, plus — when that profile differs from the picked one and both have known UGS positions — the relative direction ("finer"/"coarser") per the knowledge base's UGS ordering. The hint SHALL never present a computed grinder number for a different profile (the KB's own cross-profile rule: only direction translates). When no matching shot history exists for the chosen bean+profile pair, the grind/rpm fields SHALL fall back to the linked bag's current `grinderSetting`/`rpm` as a one-time editable default (recipe-model's "New-recipe grind defaults from the bag, once") — offered, not silently applied; the user may accept or change it before saving. With no linked bag and no history, the fields start empty. For portafilter tea, the bag's `brewTempC` SHALL seed a temperature override only when the chosen profile is not type-matched to the bag's tea type; hot-water tea SHALL use the bag's brewing numbers verbatim. Prefilled values SHALL never overwrite a value the user has already edited in this wizard session.

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

### Requirement: Drink-type-specific details fields
The details step SHALL show only the fields relevant to the drink type: espresso and filter — dose, yield, temperature, grind (recipe-owned, default-filled per the recipe model — no inherit/override toggle), equipment; americano and long black — the espresso fields plus the water-vessel picker with the order fixed by the type; latte/cappuccino — the espresso fields plus milk weight and pitcher; tea (portafilter) — leaf dose, yield, temperature, equipment, and NO grind fields; tea (hot water) — vessel, volume, temperature, leaf dose only.

#### Scenario: Tea hides grind
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** no grind or rpm fields are shown and the saved recipe stores no pinned grind

### Requirement: Summary page is the edit surface
The wizard's final step SHALL be a summary whose hero is the recipe card rendered exactly as the management page renders it (same component: photo, drink-type icon + short label, profile, bag, plan line including milk weight) — what the user builds is what they will later see in the list. The recipe name field with Cancel/Save (and any save error) SHALL sit in a header pinned above the scrolling body. Below the hero, each component SHALL render as a tappable CARD — title, value summary, single edit glyph — in a responsive card grid. The component cards SHALL be ordered so that **Equipment precedes the Dose/yield/temp/grind card**, mirroring the walk order (grinder before the grind/rpm it gates). Every stored block SHALL have a visible card — a latte's milk weight SHALL appear on the summary, not only behind an edit action. The edit glyph SHALL NOT be used with two meanings on one card (no dose→yield arrow beside a navigation arrow). Tapping a card SHALL open ITS OWN dedicated window (equipment / dose-yield-temp-grind / steam / water) and return to the summary — the same windows the creation walk uses, so create and edit present identical screens. Edit, clone, and promote-from-shot entry points SHALL open the wizard directly on the summary with all state loaded; for recipes without a stored drink type, the type SHALL be derived from the blocks and profile beverage type. Clone SHALL focus the name field for immediate rename and record clone provenance; promote-from-shot SHALL prefill from the shot record and its steam/hot-water snapshots (falling back to current settings when absent), recording shot provenance and the shot's bag. The summary SHALL retain add/remove affordances for the milk and hot-water blocks.

Each component card's value summary SHALL present the full set of values that its own editor changes, not a single reductive line, scoped to the fields the drink type actually has, and no value SHALL be repeated across cards (recipe-overridden numbers on the numbers card; profile shape on the Profile card; package identity on Equipment) — so it is obvious which card to tap to change a given value:

- **Dose, yield, temp & grind card** SHALL show every value that window pins for the drink type. For coffee/espresso family: dose, yield, the yield mode (when yield is expressed as a ratio, the ratio SHALL be shown alongside the resulting weight, e.g. "1:2.0 → 36.0g"; when a fixed target weight, that weight), effective temperature, and grind — with rpm shown only when the selected equipment's grinder is RPM-controlled; the card TITLE SHALL name grind for coffee drinks. For portafilter tea: leaf dose, yield, temperature (no grind/rpm) and a title without grind. For hot-water tea: volume and temperature (no dose→yield, no grind). The card SHALL NOT show a field the drink type does not set.
- **Bean card** SHALL show the linked bag's photo (from the existing bean-image cache, rendered per the app's image conventions — never a colour emoji in a plain `Text`) together with richer bag detail matching what the bean step's tile renders: roaster, coffee name, and roast level and/or roast date or age when present. A bag-less recipe SHALL render a clear "No bean" state.
- **Profile card** SHALL show a RICH read-out of the profile's own defining detail EXCLUDING the parameters the recipe overrides (dose, yield, temperature, grind are shown on the Dose/yield/temp/grind card and SHALL NOT be duplicated here). It SHALL show: the profile name; the profile's **editor/type classification** (e.g. Advanced, D-Flow, A-Flow, Pressure, Flow — the same classification the wizard's header line already derives); the beverage type; and a substantive summary of the profile's pressure/flow shape (e.g. the frame/step structure and its characterizing values) that the recipe does not override. The card SHALL additionally expose the same two info affordances the profile step's tiles offer, usable from the summary without leaving it: the **Profile Info button** (the "(i)" button opening the Profile Info page describing the profile) and, when the profile has a knowledge-base entry, the **knowledge-base button** (the sparkle "AI DB" popup). These affordances SHALL be distinct, clearly-purposed controls separate from the card's edit glyph. A profile-less recipe (hot-water tea) SHALL render a clear "No profile" / hot-water state.
- **Steam/milk card**, when a steam block is present, SHALL show a real summary (milk weight and pitcher, and the block's steam target/settings where set), not a bare title.
- **Equipment card** SHALL show the full equipment package (e.g. grinder model, basket, puck-prep, and other package fields) EXCLUDING the grind setting and rpm, which are recipe-owned and shown on the Dose/yield/temp/grind card. When no package is set it SHALL render "none". The equipment WINDOW SHALL present the in-inventory packages as inline, tap-to-select tiles (plus a "None" tile), highlighting the linked one — not a picker field that opens a separate dialog.

The value summaries SHALL be internationalized and SHALL degrade gracefully when a field is absent (omit the field rather than showing an empty or placeholder value).

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

### Requirement: Per-drink-type equipment default
The equipment row SHALL prefill with the equipment package most recently used on a recipe of the same drink type; when none exists, the currently active package; when none, "none". The row SHALL be changeable from the details step and the summary.

#### Scenario: Tea remembers the tea setup
- **WHEN** the user creates a second tea recipe after setting a basket-only package on the first
- **THEN** the equipment row prefills with that package

### Requirement: Name auto-suggestion from bean and drink type
The wizard SHALL suggest a recipe name composed from the bean (coffee/tea name), the drink type's short label, and the recipe's profile (e.g. "Yirgacheffe Latte · Cremina" — never a label containing a slash or parenthetical), applied only while the name field is empty or still holds the previous suggestion — never over a user edit. The suggestion SHALL update whenever the bean, drink type, or profile selection changes while the field still holds a prior suggestion.

The profile token SHALL be cleaned before use: the `D-Flow/` or `A-Flow/` editor-membership title prefix SHALL be removed, and the profile SHALL NOT be appended when its trailing word repeats the drink-type word already present (case-insensitive), mirroring the bean stutter rule. When no profile is selected (a hot-water tea recipe), no profile token SHALL be appended.

When the bean name already ends with the drink-type word (case-insensitive), the suggestion SHALL NOT append the drink-type word again.

When the composed name matches the display name of an existing non-archived recipe (a plain case-insensitive name-string match — the wizard caches existing names, not their bean/type/profile identity), the wizard SHALL append a qualifier drawn from the draft recipe's OWN dial-in values — the yield (ratio or target weight) tried first, else the dose — retrying against the name set so the suggestion stays distinct where possible. The wizard SHALL NOT disambiguate with a bare numeric counter.

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

### Requirement: Equipment precedes grind in the details layout
The details step SHALL present the equipment selection before (above) the grind/rpm fields, so the recipe's grinder rpm-capability is known — and the rpm field's visibility is correct — the first time the user reaches the grind fields, rather than only after separately visiting the equipment section further down the step.

#### Scenario: Rpm field is correct on first view
- **WHEN** the user reaches the details step for a drink type with no per-drink-type equipment default yet, and their only/selected grinder is rpm-capable
- **THEN** the equipment section (already resolved or explicitly chosen) appears before the grind fields, and the rpm field is visible when the user reaches it — not hidden until they scroll past the grind card to equipment

#### Scenario: Changing equipment after grind entry updates rpm visibility immediately
- **WHEN** the user has already entered a grind value and then changes the equipment selection to a non-rpm-capable grinder
- **THEN** the rpm field hides immediately, consistent with the newly selected equipment

### Requirement: The equipment window SHALL never open empty and SHALL be skipped when only one package exists

During the creation walk the equipment window SHALL preselect the currently
active package rather than opening with nothing chosen. When the inventory holds
exactly one in-inventory package, the wizard SHALL fill it in and skip the
equipment window entirely, advancing straight to the dose/yield/temp/grind
window — there is nothing to ask; the window stays reachable by stepping back.
Edit/clone flows keep the recipe's own package, and a summary-card jump to the
equipment window always shows it. Web recipe creation SHALL likewise link the
active package (the web form has no equipment editor; updates leave the existing
link untouched).

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

On the coffee/espresso details step, the temperature control edits an offset applied to the selected profile. The wizard SHALL display, adjacent to that offset control, the resulting brew temperature the current offset produces — so a user can dial to a target temperature without knowing the profile's default. The readout SHALL render through the same formatter the brew-settings Temp Delta uses (`TemperatureDisplay::format`, via `ProfileManager.temperatureDisplayForSteps`) against the **selected** profile's own frame temperatures — never the app's active profile — so the wizard and brew settings read identically. It SHALL therefore show at most two temperatures (a single value when the profile's frames share one temperature, two distinct values as a spaced list, or first…last for a ramp of three or more), be unit-aware (°C/°F, re-rendering when the unit setting changes), and carry a signed offset tag when the offset is non-zero. The readout SHALL be visible only when the profile's temperature is resolvable, and SHALL be exposed to assistive technology as static text. This requirement applies to the coffee/espresso path only; tea edits an absolute temperature and hot-water tea has no profile anchor.

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

