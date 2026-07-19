## MODIFIED Requirements

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
