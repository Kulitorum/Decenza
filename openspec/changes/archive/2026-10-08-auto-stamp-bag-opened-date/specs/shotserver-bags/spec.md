## ADDED Requirements

### Requirement: The web Beans page and the app share one definition of bag behaviour
The web `/beans` page SHALL NOT carry its own copy of a bag rule the app also applies: storage-type options, the card's lifecycle line and actions, restock, coffee-only fields, the bean-detail merge and the edit diff are each defined once in C++ (`CoffeeBag`, `InventoryBag`, `BeanBaseBlob`). The page gets the lists as JSON embedded at generation, and the edit route merges and diffs server-side.

#### Scenario: An edit on the web writes only what changed
- **WHEN** the web editor saves a bag after a shot stamped its `openedDate` while the editor was open
- **THEN** the save SHALL send the form as it opened alongside the current form
- **AND** the server SHALL write only the fields that differ, leaving the stamped `openedDate` in place

#### Scenario: Card actions match the app
- **WHEN** a finished bag with no shots is shown on the web page
- **THEN** it SHALL offer Delete, as the app's card does
- **AND** neither surface SHALL ask for confirmation before Bag finished or Delete

#### Scenario: A tea bag refuses coffee-only fields on every surface
- **WHEN** the app, the web API or MCP writes a non-empty roast level, grinder setting or rpm to a tea bag
- **THEN** storage SHALL refuse the write and the caller SHALL receive the reason
- **AND** clearing those fields SHALL be allowed

## MODIFIED Requirements

### Requirement: /beans web management page

The ShotServer SHALL serve a `/beans` page listing the bag inventory (open bags by default, active bag highlighted, roast dates and freshness shown) with create, edit, finish and activate actions. It SHALL match the app's Beans page in look and in features, using the shared embedded-page style and chrome, and SHALL keep the existing REST endpoints, auth gate and write-through semantics; new capabilities are additive.

#### Scenario: Edit bag from browser
- **WHEN** the user edits a bag's roast date on the web page
- **THEN** the change writes through to the bag and is visible in the app

#### Scenario: Active bag is visually highlighted
- **WHEN** the `/beans` page renders an inventory that includes the active bag
- **THEN** the active bag's card is highlighted with the accent border/style used app-wide

#### Scenario: App-matching card layout and chrome
- **WHEN** the `/beans` page loads with one or more bags
- **THEN** bags render as a responsive grid of rounded cards with the app's field hierarchy (thumbnail, coffee name + verified badge, roaster, dot-joined attributes, freshness line) and the page uses the canonical Decenza header with logo, back link, and burger menu

#### Scenario: Create a tea bag from the web
- **WHEN** the user chooses "Bag of Tea" and fills the tea fields
- **THEN** a `kind=tea` bag is created with those fields and appears in the app

#### Scenario: Link a bag via Bean Base search on the web
- **WHEN** the user searches the Bean Base from the `/beans` create/edit flow and selects a canonical record
- **THEN** the bag is linked, the card shows the verified badge, and the info popup shows the canonical details

#### Scenario: AI-import bean details on the web
- **WHEN** the user uses "get info from page" with a roaster URL on the `/beans` form
- **THEN** the form is prefilled with the extracted bean fields, matching the app's behavior

#### Scenario: The page looks like the app's

- **WHEN** the user opens `/beans`
- **THEN** it SHALL use the canonical page chrome (a `<header class="header">` with the `☕ Decenza` logo, a back link and the shared burger menu), as Shot History does
- **AND** bags SHALL render as a responsive card grid mirroring `BagCard`: thumbnail, coffee name with a verified badge when linked, roaster, a dot-joined attribute line omitting missing fields, tasting notes, a freshness/roast-date line, and a wrapping action row
- **AND** the active bag SHALL have an accent border, an empty inventory SHALL show "No bags yet" with a hint, and card, button, badge, form and modal styling SHALL come from the style shared with `/recipes` and `/equipment`

#### Scenario: The page offers the app's bean features

- **WHEN** the user works with bags on `/beans`
- **THEN** it SHALL offer separate Bag of Coffee and Bag of Tea creation (setting `kind`) with the tea fields, and every bean attribute the app edits (origin, region, farm/producer, variety, elevation, process, harvest, quality score, place of purchase, tasting notes, product link) besides roaster, coffee, roast date and roast level
- **AND** the yield anchor (grams or ratio), RPM and the per-bag equipment link
- **AND** Freeze and Thaw as card actions that ask for the date, with editable frozen, thawed and opened dates; the opened date is stamped by a portion's first shot, so there is no action for it
- **AND** Bean Base search and canonical linking via `/api/beans/search` with the verified badge and a full-detail info popup, and AI "get info from page" via `/api/beans/extract`

### Requirement: Create and update SHALL accept the full app field set

`POST /api/bags` and `POST /api/bag/<id>` SHALL accept the full app field set, including `kind` (create-only), the yield anchor (`yieldG` or `yieldRatio`, mutually exclusive), `rpmPinned`, the per-bag equipment link, the freeze-lifecycle dates and the full bean attributes. Create SHALL take only `frozenDate` and `storageHint` of those dates. Update SHALL support linking and unlinking a Bean Base canonical record.

#### Scenario: Create a tea bag with its kind
- **WHEN** a client POSTs to `/api/bags` with `kind` set to tea
- **THEN** the bag SHALL be created with kind tea

#### Scenario: Create refuses a thaw or opened date
- **WHEN** a client POSTs to `/api/bags` with a non-empty `defrostDate` or `openedDate`
- **THEN** the request SHALL be refused with the reason, since a new bag's opened date comes from its first shot

## REMOVED Requirements

### Requirement: The /beans page SHALL expose the app's full bean feature set
**Reason**: Merged back into "/beans web management page", whose "The page offers the app's bean features" scenario lists the features; "Mark Opened" no longer exists.
**Migration**: None.

### Requirement: Bag cards SHALL match the app's BagCard hierarchy
**Reason**: Merged back into "/beans web management page", whose "The page looks like the app's" scenario states the card hierarchy.
**Migration**: None.
