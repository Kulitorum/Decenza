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

`POST /api/bags` and `POST /api/bag/<id>` SHALL accept the full app field set, including `kind` (create-only), the yield anchor (`yieldG` or `yieldRatio`, mutually exclusive), `rpmPinned`, the per-bag equipment link, the freeze-lifecycle dates and the full bean attributes. Update SHALL support linking and unlinking a Bean Base canonical record.

#### Scenario: Create a tea bag with its kind

- **WHEN** a client POSTs to `/api/bags` with `kind` set to tea
- **THEN** the bag SHALL be created with kind tea

### Requirement: Bean Base search, extraction and image endpoints

Behind the same gate, the API SHALL also expose `GET /api/beans/search?q=<query>`, a read-only Bean Base lookup that reuses `BeanBaseClient` and runs off the request thread. It SHALL expose `POST /api/beans/extract`, an async extraction reusing the app's backend. It SHALL expose `GET /api/bag/<id>/image`, serving the bag's photo or a placeholder.

#### Scenario: Extraction answers on timeout

- **WHEN** a client POSTs to `/api/beans/extract` and extraction times out or is rejected
- **THEN** the server SHALL answer with an error response rather than hang

### Requirement: /beans web management page

The ShotServer SHALL serve a `/beans` page listing the bag inventory (open bags by default, active bag highlighted, roast dates and freshness shown) with create, edit, finish and activate actions. It SHALL use the canonical page chrome (`<header class="header">`, logo, back link and burger menu) and the shared embedded-page style reused across `/beans`, `/recipes` and `/equipment`.

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

### Requirement: The /beans page SHALL expose the app's full bean feature set

The page SHALL offer Bag of Coffee and Bag of Tea creation, the full bean-attribute fields, the yield anchor (grams or ratio), RPM, the per-bag equipment link, and the Thaw and Mark Opened actions. It SHALL offer Bean Base search with canonical linking and a full-detail info popup for linked bags. It SHALL offer AI "get info from page" prefilling the form from a roaster URL.

#### Scenario: Freeze actions are offered

- **WHEN** the user opens a bag on the `/beans` page
- **THEN** Thaw and Mark Opened SHALL be offered as discrete actions

### Requirement: Bag cards SHALL match the app's BagCard hierarchy

Bags SHALL render as a responsive card grid mirroring the app's `BagCard`. Each card SHALL show a bean thumbnail, the coffee name with a verified badge when linked, the roaster, a dot-joined attribute line that omits missing fields, a tasting-notes line and a freshness line. Card actions SHALL sit in a wrapping row. The active bag SHALL have an accent border, and an empty inventory SHALL show "No bags yet" with a short hint.

#### Scenario: Empty inventory shows a hint

- **WHEN** the `/beans` page loads with no bags
- **THEN** it SHALL show "No bags yet" with a short hint

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
