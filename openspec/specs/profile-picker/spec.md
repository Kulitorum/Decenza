# profile-picker Specification

## Purpose
One shared surface for finding and choosing a profile, hosted by the Profiles page and by the recipe wizard's profile step, so both offer the same search, filters, ranking and card actions.

## Requirements

### Requirement: Shared picker hosted by two surfaces
The Profiles page and the recipe wizard's profile step SHALL present the same picker: a search field, filter chips, a sort control, bean-ranked tiers and a card grid. The host SHALL decide what a card tap does (the Profiles page loads the profile; the wizard chooses it for the recipe without loading it) and the initial chip state (Favorites on for the Profiles page, no chip for the wizard). Chip and search state SHALL NOT persist across opens.

#### Scenario: Selector tap loads
- **WHEN** the user taps a card on the Profiles page
- **THEN** that profile becomes the machine's current profile and the card shows the current-profile highlight

#### Scenario: Wizard tap chooses
- **WHEN** the user taps a card on the wizard's profile step
- **THEN** the recipe's profile is set to that profile, the machine's current profile is unchanged, and the wizard advances as it does today

#### Scenario: Selector opens on Favorites
- **WHEN** the Profiles page opens
- **THEN** the Favorites chip is on, every other chip is off, and the search field is empty regardless of the previous visit

### Requirement: Composable filters
The picker SHALL offer the chips Favorites, a Source group (Built-in, Downloaded, Mine) and a Beverage group (Espresso, Filter, Tea, Maintenance). Chips within a group SHALL combine with OR, and a group with no chip on SHALL match every profile. Favorites, each group and the search text SHALL combine with AND. Search SHALL match the title only, case-insensitively as a substring, under every filter state.

#### Scenario: Cross-group AND
- **WHEN** Downloaded and Filter are on
- **THEN** only downloaded profiles whose beverage type maps to Filter are listed

#### Scenario: Within-group OR
- **WHEN** Tea and Filter are both on with no Source chip
- **THEN** every tea and every filter profile from every source is listed

#### Scenario: Search applies under any chips
- **WHEN** Built-in is on and the search text is "blo"
- **THEN** only built-in profiles whose title contains "blo" are listed

### Requirement: Beverage group membership derives from beverage_type
A profile's Beverage group SHALL derive from its `beverage_type`: `espresso` and empty or unknown map to Espresso; `filter` and `pourover` to Filter; `tea` and `tea_portafilter` to Tea; `cleaning`, `descale`, `calibrate` and `manual` to Maintenance.

#### Scenario: Unknown beverage type is treated as espresso
- **WHEN** a profile has an empty or unrecognised `beverage_type`
- **THEN** it SHALL be listed under the Espresso chip

### Requirement: Faceted chip counts
Every chip SHALL display the number of profiles that would be listed if that chip were turned on in addition to the chips already on and the current search text. Counts SHALL update whenever any chip or the search text changes.

#### Scenario: Count reflects other chips
- **WHEN** Downloaded is on and three downloaded profiles are tea
- **THEN** the Tea chip shows 3

#### Scenario: Count for an active chip
- **WHEN** a chip is on
- **THEN** it shows the size of the current result set as constrained by itself and the other active chips

### Requirement: Sort control
The picker SHALL offer an A–Z or Recently used sort, defaulting to Recently used on every open, applied to the "All" section whatever chips are on. Recently used SHALL list the current profile first, then by last shot descending, never-shot profiles last in A–Z order. The sort SHALL NOT change the favorites order; the always-visible Favorites… button opens that dialog (see `profile-favorites-order`).

#### Scenario: Default order
- **WHEN** the picker opens with no Favorites chip
- **THEN** the current profile is first, then profiles by most recent shot, then never-used profiles alphabetically

#### Scenario: Favorites order has its own door
- **WHEN** the user taps Favorites… with no chip on
- **THEN** the favorites order dialog opens, and the grid's sort is unchanged

### Requirement: Bean-ranked row above the grid
When a bean is known (current bean or wizard's chosen bag), the picker SHALL show a row headed "Recommended for ‹bean name›" (coffee name, else roaster) above "All". It SHALL list profiles used with that exact bean first, newest first, each labelled "used with ‹bean name›", then the knowledge and similar-bean recommendations, with reasons. Chips and search SHALL filter the row too. An empty row SHALL be hidden; with no bean, no row SHALL show.

#### Scenario: Selector shows the row for the current bean
- **WHEN** the Profiles page opens while a bean is set in the shot metadata and shots exist with it
- **THEN** a row headed "Recommended for" plus the bean's name lists those profiles first, most recent first, each labelled "used with" plus the bean's name

#### Scenario: Chips filter the row
- **WHEN** Tea is on and every recommended profile is espresso
- **THEN** the row is hidden

### Requirement: Card contents
Each card SHALL show the source letter (D built-in, V downloaded, U user) in the source colour, the title, the temperature and target yield, the auto-load pin when it is the auto-load profile, the knowledge sparkle when a knowledge base exists, an info button, a favorite star and an overflow (⋮) button. All cards in a grid SHALL share one height, and the current profile's card SHALL be visually highlighted.

#### Scenario: Usage line from history
- **WHEN** a profile has 12 shots, the latest 3 days ago
- **THEN** its card reads "12 shots · 3 d ago"

#### Scenario: Never used
- **WHEN** no shot names the profile
- **THEN** its card's usage line reads "Never used"

#### Scenario: Derived caption
- **WHEN** a profile's knowledge base is derived from another profile
- **THEN** the card shows the derivation caption and cards without one leave that line empty at the same height

### Requirement: Card usage, caption and title marker
A card's usage line SHALL read "N shots · X ago" from shot history, or "Never used" when no shot names the profile. A caption naming the profile its knowledge base derives from SHALL be shown when one exists, and a card without one SHALL keep that line empty at the same height. The title SHALL be prefixed by the modified marker when the profile is the current profile and has unsaved changes.

#### Scenario: Modified marker
- **WHEN** the current profile has unsaved changes
- **THEN** its card title SHALL carry the modified marker
- **AND** a card for any other profile SHALL NOT carry it

### Requirement: Card actions
The star SHALL toggle favorite membership and SHALL NOT add beyond 50 favorites. The sparkle SHALL open the knowledge dialog and the info button the Profile Info page; neither SHALL choose or load the profile. A long-press SHALL open the preview graph without choosing or loading. The ⋮ button SHALL open the profile-actions dialog (Edit, Copy, Rename for user profiles, Set/Disable Auto-Load for favorites, Delete for non-built-in), in both hosts.

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
