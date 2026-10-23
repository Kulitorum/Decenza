## ADDED Requirements

### Requirement: The web equipment surface SHALL show and restore archived packages

`GET /api/equipment` SHALL also return the archived packages, and
`POST /api/equipment/<id>/restore` SHALL restore one through `EquipmentStorage` with the
same rules as the app. The `/equipment` page SHALL label the used-package removal
action **Archive** and offer "Show archived (N)" with a **Restore** action, matching
the app.

#### Scenario: Restore from the web

- **WHEN** a client POSTs to `/api/equipment/<id>/restore` for an archived package
- **THEN** the package returns to inventory exactly as restoring it in the app would
