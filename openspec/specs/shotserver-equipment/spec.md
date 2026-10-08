# shotserver-equipment Specification

## Purpose
Covers the ShotServer equipment surface: the authenticated /api/equipment REST endpoints, puck-prep flag parity with the app, and the /equipment web management page in the shared embedded-page style.

## Requirements
### Requirement: Equipment REST API
The ShotServer SHALL expose, behind the existing authentication gate, `GET /api/equipment`, `GET /api/equipment/<id>`, `POST /api/equipment`, `POST /api/equipment/<id>`, `POST /api/equipment/<id>/remove` (soft-remove) and `POST /api/equipment/<id>/activate`. All handlers SHALL route through `EquipmentStorage`. Unused packages MAY be hard-deleted; used ones SHALL only be soft-removed.

#### Scenario: Activate package via web
- **WHEN** a client POSTs to `/api/equipment/<id>/activate`
- **THEN** the active equipment package changes exactly as selecting it in the app would

#### Scenario: Soft-remove used package
- **WHEN** a client removes a package referenced by shots
- **THEN** it is soft-removed and shot history attribution remains intact

#### Scenario: Puck-prep flags round-trip
- **WHEN** a client creates or updates a package with puck-prep flags via the web API
- **THEN** the flags persist and are returned on detail/list, matching the in-app values

### Requirement: Equipment payloads include puck-prep flags
The create and update payloads and the detail and list responses SHALL include the app's puck-prep flags (WDT, Shaker, Puck screen, Bottom paper filter, RDT spritz) alongside name, grinder brand and model, burrs, and basket brand and model. The flags SHALL round-trip through `EquipmentStorage` with the same semantics as in-app edits.


#### Scenario: Puck-prep flags round-trip
- **WHEN** a package is created through the API with WDT and Shaker set
- **THEN** its detail response reports both flags set

### Requirement: /equipment web management page
The ShotServer SHALL serve an `/equipment` page listing packages with the active one highlighted, and with create, edit, remove and activate actions, in the same embedded-page style as `/beans` and `/recipes`. All create, edit, remove and activate behaviour and write-through semantics SHALL remain unchanged.

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

### Requirement: Equipment page follows the app's visual design
The page SHALL use the ShotServer's canonical page chrome (a header with the ☕ Decenza logo, a back link and the shared burger menu), SHALL render packages as a responsive card grid, and SHALL show a friendly empty state with a short hint when no equipment exists.


#### Scenario: Empty state
- **WHEN** no equipment packages exist
- **THEN** the page shows "No equipment yet" with a short hint

### Requirement: Equipment cards match the app's summary
Each package SHALL be a rounded card titled with the package name, or grinder brand and model when unnamed, with burrs, basket and dot-joined puck-prep lines, each omitted when missing. The active package SHALL carry a distinct accent highlight, and card and form styling SHALL come from the shared embedded-page style.


#### Scenario: Active package is highlighted
- **WHEN** a package is the active package
- **THEN** its card shows a distinct accent border

### Requirement: Equipment form exposes puck-prep flags
The create and edit form SHALL expose the puck-prep flag checkboxes (WDT, Shaker, Puck screen, Bottom paper filter, RDT spritz), and the card SHALL show the resulting puck-prep line.


#### Scenario: Ticked flag appears on the card
- **WHEN** the user ticks WDT in the form and saves
- **THEN** the package's card shows WDT in its puck-prep line
