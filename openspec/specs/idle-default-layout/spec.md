# idle-default-layout Specification

## Purpose
Defines the default idle-page layout (the recipes-first centre row, the bottom bars and the Shot Plan widget), reset-to-default in the app and web editor, and how idle preset pill rows paginate within two rows.

## Requirements
### Requirement: Recipes-first default idle layout
The default idle-page layout SHALL place, in `centerTop`, Recipes, Beans, Steam and Hot Water, in that order. `centerMiddle` SHALL hold the Shot Plan widget, `bottomLeft` SHALL hold Sleep, and `bottomRight` SHALL hold Flush, History, Equipment, Profiles (type `espresso`) and Settings, in that order. The status bar, `centerStatus` and `lowerMidBar` defaults SHALL be unchanged.

#### Scenario: Fresh install gets the recipes-first layout

- **WHEN** the app starts with no stored layout configuration
- **THEN** the idle page shows Recipes, Beans, Steam, and Hot Water as the center action row, with Sleep bottom-left and Flush, History, Equipment, Profiles, Settings bottom-right

#### Scenario: No Auto-Favorites in the default

- **WHEN** the default layout is generated
- **THEN** no zone contains an `autofavorites` item (the Auto-Favorites page remains reachable for layouts that already include the widget)

### Requirement: The default keeps Profiles, Flush and Auto-Favorites out of the centre row
The Profiles button and Flush SHALL NOT appear in the default centre row. Auto-Favorites SHALL NOT appear anywhere in the default layout.

#### Scenario: Centre row holds no Profiles or Flush
- **WHEN** the default layout is generated
- **THEN** the `centerTop` zone contains no Profiles or Flush item

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
Every idle preset pill row that pages an inventory (favorite Profiles, Equipment packages, Flush presets and Hot-water vessels) SHALL show as many pills as fit within at most two rows at the row's current width, paginating the remainder with prev/next arrows. This SHALL apply in both rendering paths: the compact-bar popup and the `IdlePage` centre-zone expansion.

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

### Requirement: Pages are computed live and paging changes only visibility
The pills per page SHALL be computed live from measured pill widths and MAY differ between pages. Arrows SHALL appear only when a previous or further page exists. Paging SHALL change only which pills are visible. It SHALL NOT change the selection, load a profile, switch equipment or start an operation. Opening a row SHALL start on the first page.

#### Scenario: Everything fits, no arrows
- **WHEN** every pill of a row fits within two rows
- **THEN** no arrows appear and the row is identical to a non-paginated row

### Requirement: Selection maps between page and absolute index
For rows whose selection is an absolute index into the full list (favorite profiles, flush, hot-water vessels), selection and taps SHALL map between the page-relative pill index and the absolute index. A favorite-profile row's modified marker SHALL be included when measuring its pill width.

#### Scenario: Modified marker is measured
- **WHEN** the selected favorite profile shows its modified marker
- **THEN** the two-row fit is computed with that marker's width included

### Requirement: Equipment pages its full inventory
The equipment row SHALL select by id and SHALL keep its full MRU inventory paged, not a fixed cap.

#### Scenario: Equipment shows the whole inventory across pages
- **WHEN** the user has more than five equipment packages
- **THEN** all of them are reachable by paging, not just the five most recent

