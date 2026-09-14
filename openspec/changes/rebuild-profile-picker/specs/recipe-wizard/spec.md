## MODIFIED Requirements

### Requirement: Profile step filters by drink type and ranks by history
The profile step SHALL be the shared profile picker (see `profile-picker`), constrained by the drink type: the wizard SHALL pass the drink type's beverage filter set and the picker SHALL list only profiles whose `beverage_type` is in it: espresso, americano, long black, and latte/cappuccino → `espresso` (a missing or empty `beverage_type` SHALL be treated as espresso); filter → `filter` and `pourover`; tea → `tea_portafilter`. Maintenance beverage types SHALL never appear. While constrained, the picker SHALL NOT show its Beverage chip group; Selected, Favorites, Source chips, search and the sort control SHALL remain available. Profiles SHALL be presented in ranked tiers with visible headers: ① profiles used with this bean (exact bean identity match in shot history, most recent first), ② knowledge-driven recommendations and similar beans — coffee: profiles whose knowledge-base entry states an affinity for the bag's roast level (KB `roastAffinity`, authored only from each profile's own dial-in documentation, shown with a "suits <roast> roasts" reason label) rank first, then profiles used with same-roast-level beans; tea: type-matches between the bag's tea type and stock tea profile names rank first with a reason label, then same-tea-type history. The recommended tier SHALL be capped to a handful (5); candidates beyond the cap fall through to the final tier — ③ all remaining profiles in the filter set, ordered by the picker's sort control (default Recently used; tea additionally offers no temperature-proximity order — the sort control replaces it). ALL tiers SHALL render as the picker's cards, each carrying real profile metadata (at minimum temperature and target yield, sourced from the profile catalog cache — no per-tile file reads); tiers ① and ② additionally carry the recommendation reason as a chip on the card — never as detached right-aligned text. Each card SHALL offer the same affordances as the Profiles page card: the knowledge-base popup (sparkle icon, shown when the profile has a KB entry), the Profile Info page (the (i) button), the favorite star and the ⋮ actions dialog — all usable without selecting the profile. A search field SHALL always be available and SHALL filter within the drink type's set.

#### Scenario: Recently used profile ranks first
- **WHEN** the user picks a bean they have pulled shots with
- **THEN** the profile used most recently with that bean appears at the top under a "Used with this bean" header

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

### Requirement: Tea profile step offers "Just hot water"
For the tea drink type, the profile step SHALL include a fixed "Just hot water" card (below the ranked profiles and the grid, visible regardless of search text or chips). Selecting it SHALL produce a profile-less recipe whose drink type is hot-water tea, and the details step SHALL show only vessel, volume, temperature, and optional leaf dose.

#### Scenario: Hot-water tea recipe
- **WHEN** the user picks Tea, a tea bag, then "Just hot water"
- **THEN** the details step shows vessel/volume/temperature/leaf dose and saving succeeds with no profile

#### Scenario: Visible under filters
- **WHEN** the user has Favorites on and a search text that matches nothing
- **THEN** the "Just hot water" card is still shown
