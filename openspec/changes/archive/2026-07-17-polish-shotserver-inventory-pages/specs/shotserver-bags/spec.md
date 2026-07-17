## MODIFIED Requirements

### Requirement: Bags REST API
The ShotServer SHALL expose, behind the existing authentication gate (`shotserver_bags.cpp`): `GET /api/bags` (inventory, open bags by default with a filter for finished), `GET /api/bag/<id>` (full detail including Bean Base snapshot), `POST /api/bags` (create), `POST /api/bag/<id>` (update, using the same write-through semantics as app edits), `POST /api/bag/<id>/finish` (mark empty), and `POST /api/bag/<id>/activate` (set active bag). All handlers SHALL route through `CoffeeBagStorage`; the bag lifecycle rule SHALL be enforced (hard delete only for bags with zero shots).

To support full feature parity on the `/beans` page, the API SHALL additionally expose, behind the same auth gate:
- `GET /api/beans/search?q=<query>` — a read-only Bean Base lookup returning candidate canonical records, reusing the same backend as the app's search (`BeanBaseClient` / the MCP `bean_search` tool). The search SHALL run off the request thread and return results as JSON.
- `POST /api/beans/extract` — an **async** "get info from page" extraction that takes a roaster URL (and/or pasted page text) and returns extracted bean fields, reusing the app's extraction backend (`BeanBaseClient` / the MCP `bag_extract_details` tool). It SHALL follow the established async ShotServer pattern (`QPointer<QTcpSocket>` + fired-guard + timeout) and always emit an error response on timeout or rejection rather than hanging.
- `GET /api/bag/<id>/image` (or an equivalent bean-image route) — serving the bag's photo/thumbnail for the web card, or a suitable placeholder when none exists.

`POST /api/bags` create and `POST /api/bag/<id>` update SHALL accept the full app field set — including `kind` (coffee|tea, create-only), the yield anchor (`yieldG` or `yieldRatio`, mutually exclusive), `rpmPinned`, the per-bag equipment link, the freeze-lifecycle dates, and the full bean attributes — and `POST /api/bag/<id>` SHALL support linking/unlinking a Bean Base canonical record so the web can set the same linkage the app sets.

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

### Requirement: /beans web management page
The ShotServer SHALL serve a `/beans` page listing the bag inventory (open bags by default, active bag highlighted, roast dates/freshness shown) with create, edit, finish, and activate actions.

**Visual parity.** The page SHALL present a clean, app-matching visual design rather than a flat demo list:
- It SHALL use the ShotServer's canonical page chrome — a `<header class="header">` with the `☕ Decenza` logo, a back link, and the shared burger menu on the right — identical in structure to the Shot History page, not a bare `<div>` with a lone emoji title.
- It SHALL render the inventory as a **responsive card grid** (cards wrapping to fill the available width, one column on narrow/tablet screens), mirroring the app's `BagCard` grid.
- Each bag SHALL be a rounded surface **card** whose information hierarchy matches the app's `BagCard`: a bean **thumbnail**, the coffee name as the prominent title with a **verified badge** when the bag is linked to a Bean Base record, the roaster as a secondary line, a dense dot-joined attribute line (e.g. origin · variety · process) that omits missing fields, a tasting-notes line, and a freshness/roast-date meta line. Card actions SHALL sit in a wrapping action row.
- The **active bag** SHALL be indicated with a distinct accent border/highlight on its card, not merely a text label.
- The page SHALL show a friendly **empty state** ("No bags yet" with a short hint).
- The card, button, badge, status, form, and modal styling SHALL come from a **shared embedded-page style** reused across `/beans`, `/recipes`, and `/equipment`.

**Feature parity.** The page SHALL expose the app's full bean feature set:
- Separate **Add Coffee** and **Add Tea** creation (setting `kind`), with the app's tea fields available for tea bags.
- The full bean-attribute fields the app edits (origin, region, farm/producer, variety, elevation, process, harvest, quality score, place of purchase, tasting notes, product link) in addition to roaster/coffee/roast date/roast level.
- The **yield anchor** (grams or ratio), **RPM**, and the **per-bag equipment link**.
- The **freeze-lifecycle actions** the app offers — Thaw and Mark Opened — as discrete actions, alongside editable frozen/defrost/opened dates.
- **Bean Base search + canonical linking**: a search-first create/link flow that queries `/api/beans/search`, lets the user pick and link a canonical record, shows the verified badge, and opens a **full-detail info popup** for linked bags (matching `BeanBaseDetailsPopup`).
- **AI "get info from page"** extraction via `/api/beans/extract`, prefilling the form from a roaster URL/page, matching the app's "Get info from page" affordance.

All create/edit/finish/activate behavior, the existing REST endpoints, auth gate, and write-through semantics SHALL remain unchanged; new capabilities are additive.

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
- **WHEN** the user chooses "Add Tea" and fills the tea fields
- **THEN** a `kind=tea` bag is created with those fields and appears in the app

#### Scenario: Link a bag via Bean Base search on the web
- **WHEN** the user searches the Bean Base from the `/beans` create/edit flow and selects a canonical record
- **THEN** the bag is linked, the card shows the verified badge, and the info popup shows the canonical details

#### Scenario: AI-import bean details on the web
- **WHEN** the user uses "get info from page" with a roaster URL on the `/beans` form
- **THEN** the form is prefilled with the extracted bean fields, matching the app's behavior
