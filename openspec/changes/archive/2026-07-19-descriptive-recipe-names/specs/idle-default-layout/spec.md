## ADDED Requirements

### Requirement: Idle preset pill rows fit two rows
Every idle-screen preset pill row that pages an inventory — the favorite **Profiles** (espresso quick-select), **Equipment** packages, **Flush** presets, and **Hot-water** vessels — SHALL show as many pills as comfortably fit within **at most two rows** at the row's current available width, and SHALL paginate the remainder via prev/next arrows. This matches the Recipes (recipe-quick-switch) and Beans (bag-inventory-view) idle pill rows. The behavior SHALL apply in **both rendering paths** of each widget: the compact-bar popup (`EspressoItem`/`EquipmentItem`/`FlushItem`/`HotWaterItem`) and the `IdlePage` center-zone expansion.

The number of pills per page SHALL be computed **live** from the actual (measured) pill widths and MAY differ from one page to the next. The previous/next arrows SHALL appear only when a previous/further page exists; when every pill fits within two rows neither arrow SHALL appear and the row SHALL be visually identical to the non-paginated row. Paging SHALL change only which pills are visible — it SHALL NOT change the selection, load a profile, switch equipment, or start an operation. Opening a row SHALL start on the first page.

For rows whose selection is an absolute index into the full list (favorite profiles, flush, hot-water vessels), selection and taps SHALL map between the page-relative pill index and the absolute index so the correct item is highlighted, loaded, previewed, or started. The favorite-profile row's selected pill may carry a modified marker that widens it; its width SHALL be measured with that marker so the two-row fit stays correct. The equipment row (selection by id) SHALL keep its full MRU inventory paged rather than the previous fixed cap of five.

#### Scenario: Long names reduce the page size
- **WHEN** a row's pills (with long names) do not all fit within two rows
- **THEN** the row shows only as many as fit, paginates the rest, and shows the prev/next arrows

#### Scenario: Never more than two rows
- **WHEN** any page of any of these pill rows is shown (compact popup or center expansion)
- **THEN** the pills SHALL occupy at most two rows

#### Scenario: Paging does not change selection or start anything
- **WHEN** the user pages any of these rows
- **THEN** nothing is loaded, switched, or started and the current selection is unchanged

#### Scenario: Tap on a later page acts on the right item
- **WHEN** the user pages forward and taps a pill
- **THEN** the profile loaded/started, equipment switched, flush/hot-water preset applied is the one at that pill's absolute index, not the first-page index

#### Scenario: Equipment shows the whole inventory across pages
- **WHEN** the user has more than the previously-capped five equipment packages
- **THEN** all of them are reachable by paging, not just the five most recent
