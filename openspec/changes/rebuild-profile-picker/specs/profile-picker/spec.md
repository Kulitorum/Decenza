## Purpose

One shared surface for finding and choosing a profile, hosted by the Profiles page and by the recipe wizard's profile step, so both offer the same search, filters, ranking and card actions.

## ADDED Requirements

### Requirement: Shared picker hosted by two surfaces
The Profiles page and the recipe wizard's profile step SHALL present the same profile picker: a search field, filter chips, a sort control, bean-ranked tiers, and a card grid. The host SHALL decide what a card tap does: on the Profiles page a tap loads the profile on the machine; in the wizard a tap chooses the profile for the recipe without loading it. The host SHALL decide the initial chip state: the Profiles page opens with Selected on and nothing else; the wizard opens with no chip on. Chip and search state SHALL NOT persist across opens.

#### Scenario: Selector tap loads
- **WHEN** the user taps a card on the Profiles page
- **THEN** that profile becomes the machine's current profile and the card shows the current-profile highlight

#### Scenario: Wizard tap chooses
- **WHEN** the user taps a card on the wizard's profile step
- **THEN** the recipe's profile is set to that profile, the machine's current profile is unchanged, and the wizard advances as it does today

#### Scenario: Selector opens on Selected
- **WHEN** the Profiles page opens
- **THEN** the Selected chip is on, every other chip is off, and the search field is empty regardless of the previous visit

### Requirement: Composable filters
The picker SHALL offer these filter chips: Selected, Favorites, a Source group (Built-in, Downloaded, Mine), and a Beverage group (Espresso, Filter, Tea, Cleaning). Chips within a group SHALL combine with OR; a group with no chip on SHALL match every profile; Selected, Favorites, each group and the search text SHALL combine with AND. Beverage membership SHALL derive from the profile's `beverage_type`: `espresso` and empty/unknown → Espresso; `filter`, `pourover` → Filter; `tea`, `tea_portafilter` → Tea; `cleaning`, `descale`, `calibrate`, `manual` → Cleaning. Search SHALL match the title only, case-insensitively, as a substring, and SHALL apply in every filter state.

#### Scenario: Cross-group AND
- **WHEN** Downloaded and Filter are on
- **THEN** only downloaded profiles whose beverage type maps to Filter are listed

#### Scenario: Within-group OR
- **WHEN** Tea and Filter are both on with no Source chip
- **THEN** every tea and every filter profile from every source is listed

#### Scenario: Favorites implies Selected
- **WHEN** Favorites and Selected are both on
- **THEN** the list equals the Favorites-only list, because favoriting a profile also selects it

#### Scenario: Search applies under any chips
- **WHEN** Built-in is on and the search text is "blo"
- **THEN** only built-in profiles whose title contains "blo" are listed

### Requirement: Faceted chip counts
Every chip SHALL display the number of profiles that would be listed if that chip were turned on in addition to the chips already on and the current search text. Counts SHALL update whenever any chip or the search text changes.

#### Scenario: Count reflects other chips
- **WHEN** Downloaded is on and three downloaded profiles are tea
- **THEN** the Tea chip shows 3

#### Scenario: Count for an active chip
- **WHEN** a chip is on
- **THEN** it shows the size of the current result set as constrained by itself and the other active chips

### Requirement: Sort control
The picker SHALL offer a sort control with A–Z and Recently used, defaulting to Recently used on every open, applied to the "All" section regardless of which chips are on. With Recently used, the current profile SHALL be listed first, remaining profiles ordered by last shot descending, and profiles with no shots last in A–Z order. The sort control SHALL NOT change the favorites order setting; a separate, always-visible Favorites… button SHALL open the favorites order dialog (see `profile-favorites-order`).

#### Scenario: Default order
- **WHEN** the picker opens with no Favorites chip
- **THEN** the current profile is first, then profiles by most recent shot, then never-used profiles alphabetically

#### Scenario: Favorites order has its own door
- **WHEN** the user taps Favorites… with no chip on
- **THEN** the favorites order dialog opens, and the grid's sort is unchanged

### Requirement: Bean-ranked row above the grid
When a bean is known — the current bean on the Profiles page, the chosen bag in the wizard — the picker SHALL show one row headed "Recommended for ‹bean name›" (the bag's coffee name, else its roaster) above the "All" section. The row SHALL list profiles used with that exact bean first, most recent first, each carrying the reason "used with ‹bean name›", followed by the existing knowledge-driven and similar-bean recommendations with their reasons. Active chips and search SHALL filter the row the same way they filter the grid. An empty row SHALL be hidden. With no bean known, no row SHALL be shown.

#### Scenario: Selector shows the row for the current bean
- **WHEN** the Profiles page opens while a bean is set in the shot metadata and shots exist with it
- **THEN** a row headed "Recommended for" plus the bean's name lists those profiles first, most recent first, each labelled "used with" plus the bean's name

#### Scenario: Chips filter the row
- **WHEN** Tea is on and every recommended profile is espresso
- **THEN** the row is hidden

### Requirement: Card contents
Each card SHALL show: the source letter (D built-in, V downloaded, U user) in the source colour; a check badge when the profile is in Selected; the title, prefixed by the modified marker when it is the current profile and has unsaved changes; the profile's temperature and target yield; a usage line "N shots · X ago" (or "Never used"); a caption naming the profile its knowledge base derives from, when one exists; the auto-load pin when it is the auto-load profile; the knowledge sparkle when a knowledge base exists; an info button; a favorite star; and an overflow (⋮) button. All cards in a grid SHALL share one height. The current profile's card SHALL be visually highlighted.

#### Scenario: Usage line from history
- **WHEN** a profile has 12 shots, the latest 3 days ago
- **THEN** its card reads "12 shots · 3 d ago"

#### Scenario: Never used
- **WHEN** no shot names the profile
- **THEN** its card's usage line reads "Never used"

#### Scenario: Derived caption
- **WHEN** a profile's knowledge base is derived from another profile
- **THEN** the card shows the derivation caption and cards without one leave that line empty at the same height

### Requirement: Card actions
The star SHALL toggle favorite membership, disabled for adding when 50 favorites exist. The sparkle SHALL open the knowledge dialog; the info button SHALL open the Profile Info page; neither SHALL choose or load the profile. A long-press on the card SHALL open the profile preview popup with its graph, without choosing or loading. The ⋮ button SHALL open the accessible profile-actions dialog offering Edit, Copy, Rename (user profiles), Set/Disable Auto-Load (Selected profiles), Add to Selected / Remove from Selected by current state, and Delete (non-built-in). The same dialog SHALL be offered in both hosts.

#### Scenario: Selected toggle from the menu
- **WHEN** the user opens ⋮ on a built-in profile that is not Selected and taps "Add to Selected"
- **THEN** the profile joins the Selected list, a toast confirms it, and the card's check badge appears

#### Scenario: Long-press previews
- **WHEN** the user long-presses a card
- **THEN** the preview popup opens showing the profile graph and the current profile is unchanged

#### Scenario: Edit in the wizard
- **WHEN** the user picks Edit from ⋮ inside the wizard
- **THEN** the profile is loaded and the editor opens, and the wizard's already-chosen profile snapshot is unaffected

### Requirement: Empty state
When no profile matches, the picker SHALL show a message and a Clear filters action. Clear filters SHALL turn every chip off and empty the search text.

#### Scenario: Clear filters
- **WHEN** the user taps Clear filters
- **THEN** every chip is off, the search is empty, and the full catalogue is listed

### Requirement: Selector chrome
The Profiles page SHALL keep the auto-load strip above the picker and SHALL offer Import from Visualizer, Import from Tablet/Files and Create new profile under a single `+` menu. The page SHALL NOT show a separate favorites panel, a select checkbox on cards, or a category grouping.

#### Scenario: Add menu
- **WHEN** the user taps `+`
- **THEN** a menu offers Visualizer, Tablet (Files on iOS) and New profile
