# shotserver-recipes Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipes REST API
The ShotServer SHALL expose, behind the existing authentication gate and following the per-domain file split (`shotserver_recipes.cpp`): `GET /api/recipes` (list with active id, MRU order, shot counts, ISO 8601 last-used), `GET /api/recipe/<id>` (full detail), `POST /api/recipes` (create), `POST /api/recipe/<id>` (update), `POST /api/recipe/<id>/clone`, `POST /api/recipe/<id>/archive`, `POST /api/recipe/<id>/activate`, and `POST /api/recipes/from-shot/<shotId>` (promotion). All handlers SHALL route through `RecipeStorage` and the shared controller activation path; database reads SHALL run off the request thread per SHOTSERVER.md rules. Recipe payloads SHALL carry `drinkType` (read on list/detail; accepted on create/update, derived from blocks when omitted) and `bagId` (the linked bag: read on list/detail, accepted on create/update; list/detail SHALL also flag when the linked bag is no longer in inventory). Promotion SHALL carry the shot's bag. Create/update validation SHALL require a profile unless the payload carries a hot-water block with `hasWater` true. The web bag create (`POST /api/bags`) SHALL accept `kind` (coffee default | tea) at creation only — the bag update route SHALL never accept it.

#### Scenario: Activate via web
- **WHEN** a client POSTs to `/api/recipe/<id>/activate`
- **THEN** the app state changes exactly as an idle-screen pill tap would, and the response reports the applied recipe

#### Scenario: Lifecycle enforced
- **WHEN** a client attempts to delete a recipe that has shots
- **THEN** the API refuses and offers archive semantics instead

#### Scenario: Profile-less hot-water recipe via web
- **WHEN** a client POSTs a create with a hot-water block and no profile
- **THEN** the recipe is created; the same POST without the hot-water block is rejected with a validation error

#### Scenario: Bag link round-trips
- **WHEN** a client creates a recipe with a `bagId` and later fetches it
- **THEN** the detail response returns the same `bagId`, and after that bag is finished the response flags the link as stale

### Requirement: /recipes web management page
The ShotServer SHALL serve a `/recipes` page in the established embedded-page style listing all recipes (active highlighted) with create, edit, clone, archive, and activate actions backed by the REST API.

**Visual parity.** The page SHALL present a clean, app-matching visual design rather than a flat demo list:
- It SHALL use the ShotServer's canonical page chrome — a `<header class="header">` with the `☕ Decenza` logo, a back link, and the shared burger menu on the right — identical in structure to the Shot History page, not a bare `<div>` with a lone emoji title.
- It SHALL render recipes as a **responsive card grid** (cards wrapping to fill the available width, one column on narrow/tablet screens), mirroring the app's `RecipeDrinkCard` grid.
- Each recipe SHALL be a rounded surface **card** whose information hierarchy matches the app's `RecipeDrinkCard`: a **thumbnail**, the recipe name as the prominent title, a drink line (drink-type chip + profile title + milk), a bean line (roaster/coffee · shot count, or a stale-bag affordance when the linked bag is finished), and a plan line (dose/yield · temperature · grind). Card actions SHALL sit in a wrapping action row.
- The **active recipe** SHALL be highlighted with a distinct accent border plus an "Active" badge.
- The **drink type** SHALL be shown as a chip/label matching the app, and a **stale bag link** SHALL be surfaced as a distinct warning-styled affordance.
- **Archived recipes** SHALL remain in a separate, de-emphasized archived section with a show/hide toggle, restyled to the shared card design.
- The page SHALL show a friendly **empty state** when there are no recipes.
- The card, button, badge, status, form, and modal styling SHALL come from the **shared embedded-page style** reused across `/beans`, `/recipes`, and `/equipment`.

**Feature parity.** The page SHALL expose the app's full recipe feature set that the web currently lacks:
- A **search + sort bar** (shown when recipes exist): free-text search plus sort by the app's fields (date used / date created / coffee / profile / name) with an ascending/descending toggle.
- The **full steam block** — milk weight, pitcher, and steam duration, flow, and temperature — not just milk weight + pitcher.
- **RPM** (`rpmPinned`) editing alongside grind.
- A **stale-bag re-point action**: when a recipe's linked bag is finished/gone, the card SHALL offer a "choose beans" affordance that re-points the recipe to another open bag (kind-matched), matching the app's re-point picker.
- Recipe **card thumbnails**.

All create/edit/clone/archive/activate behavior, the REST endpoints, payloads (including `drinkType`, `bagId`, `rpmPinned`, the steam block, and the hot-water block), the auth gate, and write-through semantics SHALL remain unchanged; new UI capabilities are additive.

#### Scenario: Web edit
- **WHEN** the user edits a recipe's milk weight on the web page
- **THEN** the change persists and is visible in the app immediately

#### Scenario: Active recipe is visually highlighted
- **WHEN** the `/recipes` page renders a list that includes the active recipe
- **THEN** the active recipe's card is highlighted with the accent border/style and an "Active" badge

#### Scenario: App-matching card layout and chrome
- **WHEN** the `/recipes` page loads with one or more recipes
- **THEN** recipes render as a responsive grid of rounded cards with the app's field hierarchy (name, drink line, bean line, plan line), stale-bag links appear as a warning-styled affordance, and the page uses the canonical Decenza header

#### Scenario: Search and sort recipes on the web
- **WHEN** the user types in the search box and changes the sort field/direction
- **THEN** the recipe list filters and re-orders exactly as the app's search/sort bar does

#### Scenario: Edit the full steam block on the web
- **WHEN** the user edits a latte recipe's steam duration, flow, and temperature on the web page
- **THEN** those steam-block values persist and are reflected in the app

#### Scenario: Re-point a stale bag link on the web
- **WHEN** the user taps the "choose beans" affordance on a recipe whose linked bag is finished
- **THEN** the user can pick another open (kind-matched) bag and the recipe re-points to it, matching the app

### Requirement: Promote from the web shot browser
The web shot browser SHALL offer a "create recipe from this shot" action on shot entries, prefilled server-side the same way app promotion prefills the composer.

#### Scenario: Web promotion
- **WHEN** the user promotes a shot from the web shot list
- **THEN** a recipe is created from that shot's record with provenance recorded

### Requirement: Web recipe API round-trips the hot-water block
The ShotServer recipe API and `/recipes` management page SHALL accept, persist, and reflect a recipe's optional hot-water block, mirroring how the steam block is handled. Create and update handlers SHALL parse a `hotWater` body field into the recipe's hot-water block; recipe detail responses and the web form SHALL expose it; and promotion from a shot SHALL prefill it from the shot's hot-water snapshot when present.

#### Scenario: Web create with hot water
- **WHEN** a client creates or updates a recipe through the web API with a `hotWater` field
- **THEN** the hot-water block is persisted and reflected in the recipe's web representation and in the app immediately

#### Scenario: Web promotion carries hot water
- **WHEN** the user promotes a shot that recorded a hot-water snapshot from the web shot browser
- **THEN** the created recipe's hot-water block matches that snapshot

