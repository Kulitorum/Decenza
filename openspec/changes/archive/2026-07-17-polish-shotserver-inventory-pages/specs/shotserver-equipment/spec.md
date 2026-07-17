## MODIFIED Requirements

### Requirement: Equipment REST API
The ShotServer SHALL expose, behind the existing authentication gate (`shotserver_equipment.cpp`): `GET /api/equipment` (package inventory), `GET /api/equipment/<id>` (detail), `POST /api/equipment` (create package), `POST /api/equipment/<id>` (update), `POST /api/equipment/<id>/remove` (soft-remove, mirroring the app's mark-removed), and `POST /api/equipment/<id>/activate` (set active package). All handlers SHALL route through `EquipmentStorage`; unused packages MAY be hard-deleted, used ones only soft-removed, matching in-app behavior.

For feature parity with the app, the create/update payloads and the detail/list responses SHALL include the app's **puck-prep flags** (WDT, Shaker, Puck screen, Bottom paper filter, RDT spritz) in addition to name, grinder brand/model, burrs, and basket brand/model. The puck-prep flags SHALL round-trip through `EquipmentStorage` with the same semantics as in-app edits.

#### Scenario: Activate package via web
- **WHEN** a client POSTs to `/api/equipment/<id>/activate`
- **THEN** the active equipment package changes exactly as selecting it in the app would

#### Scenario: Soft-remove used package
- **WHEN** a client removes a package referenced by shots
- **THEN** it is soft-removed and shot history attribution remains intact

#### Scenario: Puck-prep flags round-trip
- **WHEN** a client creates or updates a package with puck-prep flags via the web API
- **THEN** the flags persist and are returned on detail/list, matching the in-app values

### Requirement: /equipment web management page
The ShotServer SHALL serve an `/equipment` page listing packages (active highlighted) with create, edit, remove, and activate actions in the same embedded-page style as `/beans` and `/recipes`.

**Visual parity.** The page SHALL present a clean, app-matching visual design rather than a flat demo list:
- It SHALL use the ShotServer's canonical page chrome — a `<header class="header">` with the `☕ Decenza` logo, a back link, and the shared burger menu on the right — identical in structure to the Shot History page, not a bare `<div>` with a lone emoji title.
- It SHALL render packages as a **responsive card grid** (cards wrapping to fill the available width, one column on narrow/tablet screens), mirroring the app's `EquipmentCard` grid.
- Each package SHALL be a rounded surface **card** whose information hierarchy matches the app's `EquipmentCard` / `EquipmentSummary`: the package name (or grinder brand+model fallback) as the prominent title, a burrs line, a basket line, and a dot-joined **puck-prep** line, each omitting missing fields. Card actions SHALL sit in a wrapping action row.
- The **active package** SHALL be indicated with a distinct accent border/highlight on its card.
- The page SHALL show a friendly **empty state** ("No equipment yet" with a short hint).
- The card, button, badge, status, form, and modal styling SHALL come from the **shared embedded-page style** reused across `/beans`, `/recipes`, and `/equipment`.

**Feature parity.** The create/edit form SHALL expose the app's **puck-prep flag** checkboxes (WDT, Shaker, Puck screen, Bottom paper filter, RDT spritz), and the card SHALL show the resulting puck-prep line, matching the app's `EquipmentSummary`.

All create/edit/remove/activate behavior, the REST endpoints, auth gate, and write-through semantics SHALL remain unchanged; new capabilities are additive.

#### Scenario: Create package from browser
- **WHEN** the user creates a grinder+basket package on the web page
- **THEN** it appears in the app's equipment inventory

#### Scenario: Active package is visually highlighted
- **WHEN** the `/equipment` page renders packages that include the active package
- **THEN** the active package's card is highlighted with the accent border/style used app-wide

#### Scenario: App-matching card layout and chrome
- **WHEN** the `/equipment` page loads with one or more packages
- **THEN** packages render as a responsive grid of rounded cards with the app's field hierarchy (name, burrs, basket, puck-prep) and the page uses the canonical Decenza header

#### Scenario: Set puck-prep flags from the web
- **WHEN** the user checks WDT and Puck screen when creating a package on the web
- **THEN** the package records those puck-prep flags and the app shows the same prep line
