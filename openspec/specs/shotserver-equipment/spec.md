# shotserver-equipment Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Equipment REST API
The ShotServer SHALL expose, behind the existing authentication gate (`shotserver_equipment.cpp`): `GET /api/equipment` (package inventory), `GET /api/equipment/<id>` (detail), `POST /api/equipment` (create package), `POST /api/equipment/<id>` (update), `POST /api/equipment/<id>/remove` (soft-remove, mirroring the app's mark-removed), and `POST /api/equipment/<id>/activate` (set active package). All handlers SHALL route through `EquipmentStorage`; unused packages MAY be hard-deleted, used ones only soft-removed, matching in-app behavior.

#### Scenario: Activate package via web
- **WHEN** a client POSTs to `/api/equipment/<id>/activate`
- **THEN** the active equipment package changes exactly as selecting it in the app would

#### Scenario: Soft-remove used package
- **WHEN** a client removes a package referenced by shots
- **THEN** it is soft-removed and shot history attribution remains intact

### Requirement: /equipment web management page
The ShotServer SHALL serve an `/equipment` page listing packages (active highlighted) with create, edit, remove, and activate actions in the same embedded-page style as `/beans` and `/recipes`.

#### Scenario: Create package from browser
- **WHEN** the user creates a grinder+basket package on the web page
- **THEN** it appears in the app's equipment inventory

