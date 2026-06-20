## ADDED Requirements

### Requirement: Shot upload includes coffee_bag_id when CM active
When a shot is uploaded and Coffee Management is active, the upload SHALL associate the shot with its Visualizer bag via a post-upload PATCH that includes `coffee_bag_id` (the upload POST itself ignores `coffee_bag_id`; the PATCH requires `Accept: application/json` and a `{"shot": {...}}` body).

#### Scenario: CM active with a known Visualizer bag
- **WHEN** CM state is `COFFEE_MANAGEMENT_ACTIVE` AND the shot's bag has a `visualizerBagId`
- **THEN** the post-upload PATCH SHALL include `coffee_bag_id: <visualizerBagId>`
- **AND** `canonical_coffee_bag_id` when linked (accepted together; linking a bag server-side also auto-sets the canonical id from the bag)

#### Scenario: CM off or unknown
- **WHEN** CM state is `NO_COFFEE_MANAGEMENT`, `PREMIUM_NO_CM`, or `UNKNOWN`
- **THEN** shot writes SHALL carry only `canonical_coffee_bag_id` (existing behaviour, no change)

### Requirement: Roaster find-or-create before bag creation
When creating a Visualizer bag, the system SHALL resolve the roaster ID before the bag POST, in order: use `beanBaseData.canonicalRoasterId` when present; else GET /api/roasters and match by `roasterName` (case-insensitive); else POST /api/roasters with `name: roasterName`; then store the resolved id in `bag.visualizerRoasterId`.

#### Scenario: Roaster already in Visualizer
- **WHEN** the roaster name matches an existing Visualizer roaster
- **THEN** no POST SHALL be made; the existing ID SHALL be used

#### Scenario: Roaster creation failure
- **WHEN** POST /api/roasters returns an error
- **THEN** the bag creation SHALL be skipped for this upload cycle
- **AND** the shot SHALL still be uploaded with `canonical_coffee_bag_id` if available

### Requirement: Bag sync is idempotent across upload cycles
Visualizer uploads are fire-and-forget — there is no automatic retry queue (only ShotServer has one), and this change does not add one; "retry" means the next upload cycle (the next shot's auto-upload or a manual re-upload). Idempotency SHALL rely on the persisted `visualizerBagId`: once set, no upload cycle ever POSTs a new bag for that coffee.

#### Scenario: Next upload cycle after partial failure
- **WHEN** a Visualizer bag was already created (stored `visualizerBagId`) but the shot PATCH failed
- **THEN** the next upload cycle SHALL use the existing `visualizerBagId` and not POST a new bag
