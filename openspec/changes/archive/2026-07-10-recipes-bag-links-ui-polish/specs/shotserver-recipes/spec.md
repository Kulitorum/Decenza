# shotserver-recipes Delta

## MODIFIED Requirements

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
