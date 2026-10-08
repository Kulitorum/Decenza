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

## MODIFIED Requirements

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
- Separate **Bag of Coffee** and **Bag of Tea** creation (setting `kind`), with the app's tea fields available for tea bags.
- The full bean-attribute fields the app edits (origin, region, farm/producer, variety, elevation, process, harvest, quality score, place of purchase, tasting notes, product link) in addition to roaster/coffee/roast date/roast level.
- The **yield anchor** (grams or ratio), **RPM**, and the **per-bag equipment link**.
- The **freeze-lifecycle action** the app offers — Thaw, available on every frozen bag including one already thawed, for the next portion — alongside editable frozen/defrost/opened dates. The opened date is stamped by a portion's first shot, so there is no separate action for it.
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
- **WHEN** the user chooses "Bag of Tea" and fills the tea fields
- **THEN** a `kind=tea` bag is created with those fields and appears in the app

#### Scenario: Link a bag via Bean Base search on the web
- **WHEN** the user searches the Bean Base from the `/beans` create/edit flow and selects a canonical record
- **THEN** the bag is linked, the card shows the verified badge, and the info popup shows the canonical details

#### Scenario: AI-import bean details on the web
- **WHEN** the user uses "get info from page" with a roaster URL on the `/beans` form
- **THEN** the form is prefilled with the extracted bean fields, matching the app's behavior
