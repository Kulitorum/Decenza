# shotserver-recipes Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipes REST API
The ShotServer SHALL expose, behind the existing authentication gate and following the per-domain file split (`shotserver_recipes.cpp`): `GET /api/recipes` (list with active id, MRU order, shot counts, ISO 8601 last-used), `GET /api/recipe/<id>` (full detail), `POST /api/recipes` (create), `POST /api/recipe/<id>` (update), `POST /api/recipe/<id>/clone`, `POST /api/recipe/<id>/archive`, `POST /api/recipe/<id>/activate`, and `POST /api/recipes/from-shot/<shotId>` (promotion). All handlers SHALL route through `RecipeStorage` and the shared controller activation path; database reads SHALL run off the request thread per SHOTSERVER.md rules.

#### Scenario: Activate via web
- **WHEN** a client POSTs to `/api/recipe/<id>/activate`
- **THEN** the app state changes exactly as an idle-screen pill tap would, and the response reports the applied recipe

#### Scenario: Lifecycle enforced
- **WHEN** a client attempts to delete a recipe that has shots
- **THEN** the API refuses and offers archive semantics instead

### Requirement: /recipes web management page
The ShotServer SHALL serve a `/recipes` page in the established embedded-page style listing all recipes (active highlighted) with create, edit, clone, archive, and activate actions backed by the REST API.

#### Scenario: Web edit
- **WHEN** the user edits a recipe's milk weight on the web page
- **THEN** the change persists and is visible in the app immediately

### Requirement: Promote from the web shot browser
The web shot browser SHALL offer a "create recipe from this shot" action on shot entries, prefilled server-side the same way app promotion prefills the composer.

#### Scenario: Web promotion
- **WHEN** the user promotes a shot from the web shot list
- **THEN** a recipe is created from that shot's record with provenance recorded

