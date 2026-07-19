# idle-default-layout Specification

## Purpose
TBD - created by archiving change recipes-idle-layout-upgrade. Update Purpose after archive.
## Requirements
### Requirement: Recipes-first default idle layout

The default idle-page layout SHALL place, in the `centerTop` zone, exactly: **Recipes, Beans, Steam, Hot Water** (in that order); in the `centerMiddle` zone: the Shot Plan widget; in the `bottomLeft` zone: Sleep; and in the `bottomRight` zone: **Flush, History, Equipment, Profiles (type `espresso`), Settings** (in that order). The status bar, `centerStatus`, and `lowerMidBar` zone defaults SHALL be unchanged from their current composition. The Profiles button and Flush SHALL NOT appear in the default center row, and Auto-Favorites SHALL NOT appear anywhere in the default layout.

#### Scenario: Fresh install gets the recipes-first layout

- **WHEN** the app starts with no stored layout configuration
- **THEN** the idle page shows Recipes, Beans, Steam, and Hot Water as the center action row, with Sleep bottom-left and Flush, History, Equipment, Profiles, Settings bottom-right

#### Scenario: No Auto-Favorites in the default

- **WHEN** the default layout is generated
- **THEN** no zone contains an `autofavorites` item (the Auto-Favorites page remains reachable for layouts that already include the widget)

### Requirement: Reset to default applies the recipes-first layout

The whole-layout "Reset to default" actions — in the in-app layout settings and in the web layout editor — SHALL replace the stored layout with the recipes-first default composition.

#### Scenario: In-app reset

- **WHEN** the user confirms "Reset to default" in the layout settings
- **THEN** the stored layout is replaced with the recipes-first default and the idle page re-renders accordingly

#### Scenario: Web editor reset

- **WHEN** the user clicks "Reset to Default" in the web layout editor
- **THEN** the same recipes-first default is applied (both paths call the same reset)

### Requirement: Injection migrations do not distort the new default

The pre-existing layout injection migrations (equipment, recipes) SHALL remain no-ops on the new default layout: since the default already contains both widgets, loading a freshly reset layout SHALL NOT insert duplicates or reorder items.

#### Scenario: Reset layout survives a reload unchanged

- **WHEN** the layout is reset to default and then reloaded from storage
- **THEN** the zone contents are identical to the default composition (no injected duplicates)

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

