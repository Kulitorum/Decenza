# shotserver-recipes Specification

## Purpose
TBD - created by archiving change add-recipes. Update Purpose after archive.
## Requirements
### Requirement: Recipes REST API
The ShotServer SHALL expose, behind the existing authentication gate and following the per-domain file split (`shotserver_recipes.cpp`): `GET /api/recipes` (list with active id, MRU order, shot counts, ISO 8601 last-used), `GET /api/recipe/<id>` (full detail), `POST /api/recipes` (create), `POST /api/recipe/<id>` (update), `POST /api/recipe/<id>/clone`, `POST /api/recipe/<id>/archive`, `POST /api/recipe/<id>/activate`, and `POST /api/recipes/from-shot/<shotId>` (promotion). All handlers SHALL route through `RecipeStorage` and the shared controller activation path; database reads SHALL run off the request thread per SHOTSERVER.md rules. Recipe payloads SHALL carry `drinkType` (read on list/detail; accepted on create/update, derived from blocks when omitted). Create/update validation SHALL require a profile unless the payload carries a hot-water block with `hasWater` true. The web bag create (`POST /api/bags`) SHALL accept `kind` (coffee default | tea) at creation only — the bag update route SHALL never accept it.

#### Scenario: Activate via web
- **WHEN** a client POSTs to `/api/recipe/<id>/activate`
- **THEN** the app state changes exactly as an idle-screen pill tap would, and the response reports the applied recipe

#### Scenario: Lifecycle enforced
- **WHEN** a client attempts to delete a recipe that has shots
- **THEN** the API refuses and offers archive semantics instead

#### Scenario: Profile-less hot-water recipe via web
- **WHEN** a client POSTs a create with a hot-water block and no profile
- **THEN** the recipe is created; the same POST without the hot-water block is rejected with a validation error

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

### Requirement: Web recipe API round-trips the hot-water block
The ShotServer recipe API and `/recipes` management page SHALL accept, persist, and reflect a recipe's optional hot-water block, mirroring how the steam block is handled. Create and update handlers SHALL parse a `hotWater` body field into the recipe's hot-water block; recipe detail responses and the web form SHALL expose it; and promotion from a shot SHALL prefill it from the shot's hot-water snapshot when present.

#### Scenario: Web create with hot water
- **WHEN** a client creates or updates a recipe through the web API with a `hotWater` field
- **THEN** the hot-water block is persisted and reflected in the recipe's web representation and in the app immediately

#### Scenario: Web promotion carries hot water
- **WHEN** the user promotes a shot that recorded a hot-water snapshot from the web shot browser
- **THEN** the created recipe's hot-water block matches that snapshot

