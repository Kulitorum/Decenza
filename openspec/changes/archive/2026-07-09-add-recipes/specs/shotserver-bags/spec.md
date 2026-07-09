# shotserver-bags

## ADDED Requirements

### Requirement: Bags REST API
The ShotServer SHALL expose, behind the existing authentication gate (`shotserver_bags.cpp`): `GET /api/bags` (inventory, open bags by default with a filter for finished), `GET /api/bag/<id>` (full detail including Bean Base snapshot), `POST /api/bags` (create), `POST /api/bag/<id>` (update, using the same write-through semantics as app edits), `POST /api/bag/<id>/finish` (mark empty), and `POST /api/bag/<id>/activate` (set active bag). All handlers SHALL route through `CoffeeBagStorage`; the bag lifecycle rule SHALL be enforced (hard delete only for bags with zero shots).

#### Scenario: Finish a bag via web
- **WHEN** a client POSTs to `/api/bag/<id>/finish` for a used bag
- **THEN** the bag is marked empty (not deleted) and leaves the app's inventory pills

#### Scenario: Delete guard
- **WHEN** a client attempts to delete a bag that has shots
- **THEN** the API refuses, mirroring the in-app rule

### Requirement: /beans web management page
The ShotServer SHALL serve a `/beans` page listing the bag inventory (active bag highlighted, roast dates/freshness shown) with create, edit, finish, and activate actions. The v1 page SHALL be plain form fields (no URL/AI import).

#### Scenario: Edit bag from browser
- **WHEN** the user edits a bag's roast date on the web page
- **THEN** the change writes through to the bag and is visible in the app
