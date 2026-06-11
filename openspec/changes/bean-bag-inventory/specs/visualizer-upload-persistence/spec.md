## MODIFIED Requirements (delta from existing Visualizer upload behaviour)

### Requirement: Shot upload includes coffee_bag_id when CM active (modification)
This spec modifies the existing Visualizer upload flow. When a shot is uploaded and Coffee Management is active, the upload payload SHALL include `coffee_bag_id` in addition to (or replacing) `canonical_coffee_bag_id`.

#### Before this change
- The upload cycle is a single multipart POST to /api/shots/upload. `canonical_coffee_bag_id` is sent only by the metadata PATCH (`updateShotOnVisualizer`), which runs on later user edits — not as part of every upload — and only when `beanBaseJson` is non-empty.
- No Visualizer bag objects are created per shot; the bag association is purely via the canonical ID.

#### After this change
- The upload cycle gains a post-upload PATCH bag-linking step (spike-verified: the upload POST ignores `coffee_bag_id`; the PATCH requires `Accept: application/json` and a `{"shot": {...}}` body).
- **WHEN** CM state is `COFFEE_MANAGEMENT_ACTIVE` AND the shot's bag has a `visualizerBagId`
- **THEN** the post-upload PATCH SHALL include `coffee_bag_id: <visualizerBagId>`
- **AND** `canonical_coffee_bag_id` when linked (spike-verified accepted together; linking a bag server-side also auto-sets the canonical id from the bag)

- **WHEN** CM state is `NO_COFFEE_MANAGEMENT` or `PREMIUM_NO_CM` or `UNKNOWN`
- **THEN** shot writes SHALL carry only `canonical_coffee_bag_id` (existing behaviour, no change)

### Requirement: Roaster find-or-create before bag creation
When creating a Visualizer bag (new requirement from `visualizer-coffee-management` spec), the system SHALL resolve the roaster ID before the bag POST.

#### Resolution order
1. If `beanBaseData.canonicalRoasterId` is present, use it directly — Visualizer accepts canonical roaster IDs
2. Otherwise GET /api/roasters and match by `roasterName` (case-insensitive)
3. If not found, POST /api/roasters with `name: roasterName` and use the returned ID
4. Store the roaster ID in `bag.visualizerRoasterId`

#### Scenario: Roaster already in Visualizer
- **WHEN** the roaster name matches an existing Visualizer roaster
- **THEN** no POST SHALL be made; the existing ID SHALL be used

#### Scenario: Roaster creation failure
- **WHEN** POST /api/roasters returns an error
- **THEN** the bag creation SHALL be skipped for this upload cycle
- **AND** the shot SHALL still be uploaded with `canonical_coffee_bag_id` if available

### Requirement: Bag sync is idempotent across upload cycles
Visualizer uploads are fire-and-forget — there is no automatic retry queue (only ShotServer has one), and this change does not add one. "Retry" means the next upload cycle: the next shot's auto-upload or a manual re-upload. Idempotency SHALL rely on the persisted `visualizerBagId`: once set, no upload cycle ever POSTs a new bag for that coffee.

#### Scenario: Next upload cycle after partial failure
- **WHEN** a Visualizer bag was already created (stored `visualizerBagId`) but the shot PATCH failed
- **THEN** the next upload cycle SHALL use the existing `visualizerBagId` and not POST a new bag
