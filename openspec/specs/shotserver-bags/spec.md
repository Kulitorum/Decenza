# shotserver-bags Specification

## Purpose
The ShotServer web `/beans` page: the bag inventory as the app shows it, with the same fields and actions, served by the running app to a browser on the network.

## Requirements

### Requirement: Bags REST API

The ShotServer SHALL expose, behind the existing auth gate (`shotserver_bags.cpp`), `GET /api/bags` (open bags by default), `GET /api/bag/<id>` (full detail), `POST /api/bags` (create), `POST /api/bag/<id>` (update, with the same write-through semantics as app edits), `POST /api/bag/<id>/finish` and `POST /api/bag/<id>/activate`. All handlers SHALL route through `CoffeeBagStorage`, and hard delete SHALL be allowed only for bags with zero shots.

#### Scenario: Finish a bag via web
- **WHEN** a client POSTs to `/api/bag/<id>/finish` for a used bag
- **THEN** the bag is marked empty (not deleted) and leaves the app's inventory pills

#### Scenario: Delete guard
- **WHEN** a client attempts to delete a bag that has shots
- **THEN** the API refuses, mirroring the in-app rule

#### Scenario: Bean Base search from the web
- **WHEN** a client GETs `/api/beans/search?q=<roaster or coffee>`
- **THEN** the response returns Bean Base candidate records equivalent to the app's search, computed off the request thread

#### Scenario: AI page-extraction from the web
- **WHEN** a client POSTs a roaster URL to `/api/beans/extract`
- **THEN** the server runs the same extraction the app runs and returns the extracted bean fields, or an error response on timeout/rejection (never a hung request)

#### Scenario: Link a bag to a Bean Base record from the web
- **WHEN** a client updates a bag with a Bean Base canonical id via the web API
- **THEN** the bag is linked to that canonical record exactly as an in-app link would, and the link is reflected in the app

### Requirement: Create and update SHALL accept the full app field set

`POST /api/bags` and `POST /api/bag/<id>` SHALL accept the full app field set, including `kind` (create-only), the yield anchor (`yieldG` or `yieldRatio`, mutually exclusive), `rpmPinned`, the per-bag equipment link, the freeze-lifecycle dates and the full bean attributes. Create SHALL take only `frozenDate` and `storageHint` of those dates. Update SHALL support linking and unlinking a Bean Base canonical record.

#### Scenario: Create a tea bag with its kind
- **WHEN** a client POSTs to `/api/bags` with `kind` set to tea
- **THEN** the bag SHALL be created with kind tea

#### Scenario: Create refuses a thaw or opened date
- **WHEN** a client POSTs to `/api/bags` with a non-empty `defrostDate` or `openedDate`
- **THEN** the request SHALL be refused with the reason, since a new bag's opened date comes from its first shot

### Requirement: Bean Base search, extraction and image endpoints

Behind the same gate, the API SHALL also expose `GET /api/beans/search?q=<query>`, a read-only Bean Base lookup that reuses `BeanBaseClient` and runs off the request thread. It SHALL expose `POST /api/beans/extract`, an async extraction reusing the app's backend. It SHALL expose `GET /api/bag/<id>/image`, serving the bag's photo or a placeholder.

#### Scenario: Extraction answers on timeout

- **WHEN** a client POSTs to `/api/beans/extract` and extraction times out or is rejected
- **THEN** the server SHALL answer with an error response rather than hang

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

### Requirement: Finished bags on the web Beans page

`GET /api/bags/finished` SHALL return the finished bags in the same shape as `GET /api/bags`. `POST /api/bag/<id>/restore` SHALL return a finished bag to inventory. The `/beans` page SHALL show a "Show finished (N)" toggle listing them as dimmed cards with Restock, Restore, Edit and Info, and Restock SHALL open the new-bag editor prefilled from the finished bag as the app does; open bags SHALL offer Restock too. A failed bag read SHALL answer 500, never an empty list or "Bag not found".

#### Scenario: Restock from the web
- **WHEN** the user taps Restock on a finished bag on the web Beans page
- **THEN** the editor opens as a new bag with that bag's identity and details, its dates and notes blank

### Requirement: Web Beans search and sort

The `/beans` web page SHALL offer the search field and sort controls of the web `/recipes` page, matching bags as the app does (every text value and the kind, never identifiers, links or enum values) and filtering the finished bags too. It SHALL offer the app's sort keys in the same order, open in the device's saved sort, and save a sort chosen there as the same preference. It SHALL say nothing matches only when neither the open nor the finished bags match.

#### Scenario: Search on the web
- **WHEN** the user types a tasting note into the `/beans` search
- **THEN** only bags carrying that note are listed, finished ones included

#### Scenario: Matches only among finished bags
- **WHEN** a web search matches only finished bags
- **THEN** the page does not say nothing matches, and "Show finished (N)" counts the matches

#### Scenario: Web page shares the sort
- **WHEN** the user picks a sort on `/beans` and then opens Beans in the app
- **THEN** the app shows the bags in that order

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
