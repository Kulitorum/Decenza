# shotserver-recipes Specification

## Purpose
Specifies the ShotServer recipe REST API (list, detail, create, update, clone, archive, activate and promote-from-shot), the bag kind rule on the web bag routes, and the `/recipes` web management page with its visual and feature parity with the app.

## Requirements
### Requirement: Recipes REST API
The ShotServer SHALL expose, behind the existing auth gate in `shotserver_recipes.cpp`, `GET /api/recipes`, `GET /api/recipe/<id>`, `POST /api/recipes`, `POST /api/recipe/<id>`, `POST /api/recipe/<id>/clone`, `POST /api/recipe/<id>/archive`, `POST /api/recipe/<id>/activate` and `POST /api/recipes/from-shot/<shotId>`. Handlers SHALL route through `RecipeStorage` and the shared activation path. Database reads SHALL run off the request thread.

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

### Requirement: Recipe list payload
`GET /api/recipes` SHALL list recipes with the active id, most-recently-used order, shot counts and ISO 8601 last-used times.

#### Scenario: List carries the active id and MRU order
- **WHEN** a client requests `/api/recipes`
- **THEN** the response names the active recipe id and orders recipes most recently used first

### Requirement: Recipe payloads carry drink type and bag link
Recipe payloads SHALL carry `drinkType`, read on list and detail and accepted on create and update, derived from the blocks when omitted. They SHALL carry `bagId`, read and accepted likewise. List and detail SHALL flag a linked bag that is no longer in inventory. Promotion SHALL carry the shot's bag.

#### Scenario: Promotion carries the shot's bag
- **WHEN** a client promotes a shot that was pulled with a bag
- **THEN** the created recipe's `bagId` is that shot's bag

### Requirement: Create and update validation
Create and update SHALL require a profile unless the payload carries a hot-water block with `hasWater` true.

#### Scenario: Create without a profile or hot water is refused
- **WHEN** a client creates a recipe with neither a profile nor a hot-water block
- **THEN** the request is rejected with a validation error

### Requirement: Bag kind is set only at creation
The web bag create (`POST /api/bags`) SHALL accept `kind` (coffee by default, or tea) at creation only. The bag update route SHALL never accept it.

#### Scenario: Kind set at creation only
- **WHEN** a client sends `kind` to the bag update route
- **THEN** the field is not accepted, and the bag's kind is unchanged

### Requirement: /recipes web management page
The ShotServer SHALL serve a `/recipes` page in the established embedded-page style, listing all recipes with the active one highlighted, and with create, edit, clone, archive and activate actions backed by the REST API. All create, edit, clone, archive and activate behaviour, the REST payloads and write-through semantics SHALL remain unchanged. New UI capabilities are additive.

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

### Requirement: Page chrome and card grid match the app
The page SHALL use the canonical chrome: a `<header class="header">` with the `☕ Decenza` logo, a back link and the shared burger menu, identical in structure to the Shot History page. Recipes SHALL render as a responsive card grid, one column on narrow screens, mirroring the app's `RecipeDrinkCard` grid. Styling SHALL come from the shared embedded-page style used by `/beans`, `/recipes` and `/equipment`.

#### Scenario: Page uses the shared chrome
- **WHEN** the `/recipes` page loads
- **THEN** it shows the canonical Decenza header and styles its cards from the shared embedded-page style

### Requirement: Recipe card hierarchy
Each recipe card SHALL show a thumbnail, the recipe name as the prominent title, a drink line (drink-type chip, profile title and milk), a bean line (roaster and coffee with shot count, or a stale-bag affordance when the linked bag is finished) and a plan line (dose and yield, temperature, grind). Card actions SHALL sit in a wrapping action row.

#### Scenario: Card shows the app's field hierarchy
- **WHEN** a recipe card renders for a recipe with a linked bag
- **THEN** the card shows the name, drink, bean and plan lines in that order, with the actions in a wrapping row

### Requirement: Active, archived and empty states
The active recipe SHALL be highlighted with a distinct accent border and an "Active" badge. The drink type SHALL show as a chip. A stale bag link SHALL show as a distinct warning-styled affordance. Archived recipes SHALL sit in a separate de-emphasised section with a show and hide toggle. With no recipes, the page SHALL show a friendly empty state.

#### Scenario: Archived recipes are hidden behind a toggle
- **WHEN** the page shows archived recipes
- **THEN** they sit in their own section, which the user can show or hide

### Requirement: Search and sort bar
When recipes exist, the page SHALL show a search and sort bar: free-text search, and sort by date used, date created, coffee, profile or name, with an ascending and descending toggle. The list SHALL filter and order exactly as the app's bar does.

#### Scenario: Empty page shows the empty state
- **WHEN** the user has no recipes
- **THEN** the page shows the empty state and no search and sort bar

### Requirement: Full steam block, RPM and stale-bag re-point
The page SHALL expose the full steam block (milk weight, pitcher, steam duration, flow and temperature), RPM (`rpmPinned`) alongside grind, and recipe card thumbnails. A recipe whose linked bag is finished SHALL offer a "choose beans" affordance that re-points it to another open bag of matching kind, as the app's re-point picker does.

#### Scenario: Steam block and RPM are editable on the web
- **WHEN** the user edits a recipe's steam flow and RPM on the web page
- **THEN** the values persist and are reflected in the app

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

