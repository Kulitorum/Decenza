## ADDED Requirements

### Requirement: Favorite-profile idle pills fit two rows
The idle-screen favorite-profile pill row (the "espresso" quick-select, both the compact-bar popup in `EspressoItem` and the center-zone expansion in `IdlePage`) SHALL show as many favorite-profile pills as comfortably fit within **at most two rows** at the row's current available width, and SHALL paginate the remainder via prev/next arrows — matching the Recipes (recipe-quick-switch) and Beans (bag-inventory-view) idle pill rows. The number of pills per page SHALL be computed **live** from the actual (measured) pill widths and MAY differ from one page to the next. Paging SHALL change only which favorites are visible — it SHALL NOT change the selected favorite or load a profile. The previous/next arrows SHALL appear only when a previous/further page exists; when every favorite fits within two rows neither arrow SHALL appear. Selection and taps SHALL map between the page-relative pill index and the absolute `selectedFavoriteProfile` index so the correct profile is highlighted, loaded, previewed, or started.

#### Scenario: Long profile names reduce the page size
- **WHEN** the user has many favorite profiles, or profiles with long names, such that they do not all fit within two rows
- **THEN** the pill row shows only as many as fit, paginates the rest, and shows the prev/next arrows

#### Scenario: Never more than two rows
- **WHEN** any page of favorite-profile pills is shown (in either the compact popup or the center expansion)
- **THEN** the pills SHALL occupy at most two rows

#### Scenario: Paging does not change selection
- **WHEN** the user pages the favorite-profile pills
- **THEN** no profile is loaded or started and the selected favorite is unchanged

#### Scenario: Tap on a later page loads the right profile
- **WHEN** the user pages forward and taps a pill
- **THEN** the profile loaded/started is the one at the absolute favorite index for that pill, not the first-page index
