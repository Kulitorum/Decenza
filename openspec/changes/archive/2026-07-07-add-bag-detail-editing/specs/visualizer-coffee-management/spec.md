# visualizer-coffee-management Delta

## ADDED Requirements

### Requirement: Bag edits auto-push to the linked Visualizer bag

When a bag save (bag editor confirm or MCP `bag_update`) changes any Visualizer-mapped field and the bag carries a non-empty `visualizerBagId` and Visualizer credentials exist, the system SHALL send `PATCH /api/coffee_bags/{visualizerBagId}` with the mapped field set at current values (last-writer-wins for fields we hold; empty local values are omitted, never sent as null, so a local clear does not wipe a server-side value — distinct from the upload-time enrichment, which remains blank-fill-only). The mapping: `coffeeName`→`name`, `roastDate`→`roast_date`, `roastLevel`→`roast_level`, `frozenDate`→`frozen_date`, `defrostDate`→`defrosted_date`, `notes`→`notes`, `origin`→`country`, `region`→`region`, `farm`→`farm`, `producer`→`farmer`, `variety`→`variety`, `elevation`→`elevation`, `process`→`processing`, `harvest`→`harvest_time`, `qualityScore`→`quality_score`, `placeOfPurchase`→`place_of_purchase`, `tastingNotes`→`tasting_notes`, `link`→`url`, `beanBaseId`→`canonical_coffee_bag_id`. A roaster name change re-resolves the roaster (find-or-create by name, as the shipped push already does) and re-points `roaster_id` when it changed. Dose/grind write-through writes SHALL NOT trigger a push (they are not Visualizer-stored fields — the shipped `touchesVisualizerFields` gate).

#### Scenario: Successful push on edit
- **WHEN** the user edits a linked bag's tasting notes and URL in the bag editor and saves, and the PATCH returns 200
- **THEN** the Visualizer bag SHALL carry the new values
- **AND** `visualizerSyncPending` SHALL be false

#### Scenario: Bag without a remote id
- **WHEN** a bag with no `visualizerBagId` is edited
- **THEN** no PATCH SHALL be sent — the existing upload-time find-or-create covers it on the next shot upload

#### Scenario: Dose or grind write-through does not push
- **WHEN** the user adjusts dose or grind setting (bean setters writing through to the bag row)
- **THEN** no Visualizer PATCH SHALL be sent

### Requirement: Edit-push failure handling

A retryable push failure (network error, 429, 5xx) SHALL set the bag's `visualizerSyncPending` flag; the next upload cycle SHALL re-push pending bags with the same full-body PATCH and clear the flag on success (event-driven, no timers). A 403 SHALL clear the flag and cache CM state as `NO_COFFEE_MANAGEMENT` (bag CRUD is premium-gated — same handling as the shipped create/enrich paths; a connection test resets it). A 404 SHALL clear the flag (stale remote id; the next shot upload re-creates and re-links). A 422 (e.g. name+roast_date uniqueness collision, defrost-before-frozen) SHALL clear the flag and surface a non-blocking notification with the server's message — local values stay as edited, no retry loop.

#### Scenario: Offline edit retried at next upload
- **WHEN** a bag edit's PATCH fails with a network error and a shot is later uploaded
- **THEN** the upload cycle SHALL re-send the bag PATCH and clear `visualizerSyncPending` on 200

#### Scenario: Rename collides with an existing remote bag
- **WHEN** the push returns 422 for a name+roast_date collision
- **THEN** the local edit SHALL be kept, the flag cleared, and the user notified once — no repeated retries

#### Scenario: Premium lapsed
- **WHEN** the push returns 403
- **THEN** the flag SHALL be cleared and CM state cached as `NO_COFFEE_MANAGEMENT`, suppressing further edit-time pushes until a connection test resets the state

## MODIFIED Requirements

### Requirement: Visualizer bag creation at upload time (CM active only)
When CM state is `COFFEE_MANAGEMENT_ACTIVE` and a shot is uploaded with a linked local bag that has no `visualizerBagId`, the system SHALL find-or-create the remote roaster and bag, then link the shot.

#### Scenario: Roaster resolution order
- **WHEN** a remote bag must be created
- **THEN** the roaster SHALL be resolved: `beanBaseData.canonicalRoasterId` passed as `canonical_roaster_id` on creation when present; an existing roaster matched by name via GET /api/roasters; else POST /api/roasters `{name}` — storing the id in `bag.visualizerRoasterId`

#### Scenario: Bag find-or-create
- **WHEN** the roaster id is known
- **THEN** the system SHALL first look for an existing remote bag matching name + roast_date in `GET /api/coffee_bags?roaster_id=<id>` (the server's own CM-enable job dedupes on roaster+name+roast_date; the API create endpoint does not)
- **AND** only POST a new bag when no match exists, with the verified field names: `name`, `roaster_id`, `roast_date`, `roast_level`, `country`, `region`, `farm`, `farmer` (our producer), `variety`, `processing`, `harvest_time`, `quality_score`, `place_of_purchase`, `tasting_notes`, `elevation`, `url` (our link), `frozen_date`, `defrosted_date` (server name for our defrostDate), `notes`, `canonical_coffee_bag_id`
- **AND** store the returned UUID in `bag.visualizerBagId`
- **AND** `startWeightG` SHALL NOT be sent (no server field; the server's free-form `metadata` belongs to the user)

#### Scenario: Shot link via post-upload PATCH
- **WHEN** the upload POST succeeds and CM is active with a known `visualizerBagId`
- **THEN** the system SHALL PATCH the shot with `coffee_bag_id` (and `canonical_coffee_bag_id` when linked — always accepted) using `Accept: application/json` and the `{"shot": {...}}` body — the upload POST itself ignores `coffee_bag_id` (verified)
