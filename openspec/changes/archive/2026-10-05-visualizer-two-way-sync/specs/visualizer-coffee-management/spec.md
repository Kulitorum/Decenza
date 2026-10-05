## ADDED Requirements

### Requirement: A bag edit is pushed without waiting for a shot upload

A bag edit that changes a Visualizer-mapped field (from the bag editor, an AI fill or MCP `bag_update`) SHALL be pushed to the linked Visualizer bag at once unless Coffee Management is known to be off for the account. It SHALL NOT wait for a shot upload to confirm Coffee Management. While Coffee Management is unconfirmed, a roaster rename SHALL re-point only to an existing roaster and SHALL NOT create one. A bag whose push failed SHALL be retried after a shot upload and at the end of each edit-pull pass.

#### Scenario: AI fill on a device that does not upload shots
- **GIVEN** the desktop app, with no shot uploaded this session
- **WHEN** the user fills a synced bag's details with AI and saves
- **THEN** the bag's fields reach Visualizer without any shot being uploaded

## MODIFIED Requirements

### Requirement: Bag edits auto-push to the linked Visualizer bag

When a bag save (bag editor confirm or MCP `bag_update`) changes any Visualizer-mapped field and the bag carries a non-empty `visualizerBagId` and Visualizer credentials exist, the system SHALL send `PATCH /api/coffee_bags/{visualizerBagId}` carrying only the mapped fields whose local value differs from the value Visualizer was last known to hold (`coffee_bags.visualizer_seen`); a field cleared locally SHALL be sent as `null`, except the name and the canonical link, which SHALL NOT be sent as `null`. A field Visualizer has never been seen to hold SHALL be sent only when it is set locally, so a bag that predates this tracking never wipes a server-side value. After a 200 the sent values SHALL be recorded as seen. When nothing differs and no roaster or archive change is due, no PATCH SHALL be sent. The mapping: `coffeeName`→`name`, `roastDate`→`roast_date`, `roastLevel`→`roast_level`, `frozenDate`→`frozen_date`, `defrostDate`→`defrosted_date`, `notes`→`notes` (as HTML), `origin`→`country`, `region`→`region`, `farm`→`farm`, `producer`→`farmer`, `variety`→`variety`, `elevation`→`elevation`, `process`→`processing`, `harvest`→`harvest_time`, `qualityScore`→`quality_score`, `placeOfPurchase`→`place_of_purchase`, `tastingNotes`→`tasting_notes`, `link`→`url`, `beanBaseId`→`canonical_coffee_bag_id`. A roaster name change re-resolves the roaster and re-points `roaster_id` when it changed. Dose/grind write-through writes SHALL NOT trigger a push (they are not Visualizer-stored fields — the shipped `touchesVisualizerFields` gate).

#### Scenario: Successful push on edit
- **WHEN** the user edits a linked bag's tasting notes and URL in the bag editor and saves, and the PATCH returns 200
- **THEN** the PATCH carries those two fields only, the Visualizer bag carries the new values
- **AND** `visualizerSyncPending` SHALL be false

#### Scenario: Local clear reaches Visualizer
- **GIVEN** a bag whose notes Visualizer was last seen to hold
- **WHEN** the user clears the notes in Decenza
- **THEN** the PATCH sends `notes: null`, and a later pull does not restore them

#### Scenario: An unpulled Visualizer edit survives
- **GIVEN** the user changed the bag's region on Visualizer since the last pull
- **WHEN** the user edits its tasting notes in Decenza
- **THEN** the PATCH carries the tasting notes and not the region

#### Scenario: Bag without a remote id
- **WHEN** a bag with no `visualizerBagId` is edited
- **THEN** no PATCH SHALL be sent — the existing upload-time find-or-create covers it on the next shot upload

#### Scenario: Dose or grind write-through does not push
- **WHEN** the user adjusts dose or grind setting (bean setters writing through to the bag row)
- **THEN** no Visualizer PATCH SHALL be sent

### Requirement: Edit-push failure handling

A retryable push failure (network error, 429, 5xx) SHALL set the bag's `visualizerSyncPending` flag; the bag SHALL be re-pushed after the next shot upload and at the end of each edit-pull pass, with the fields that still differ from what Visualizer was last seen to hold, and the flag cleared on success (event-driven, no timers). A 403 SHALL clear the flag and cache CM state as `NO_COFFEE_MANAGEMENT` (bag CRUD is premium-gated — same handling as the shipped create/enrich paths; a connection test resets it). A 404 SHALL clear the flag (stale remote id; the next shot upload re-creates and re-links). A 422 (e.g. name+roast_date uniqueness collision, defrost-before-frozen) SHALL clear the flag and surface a non-blocking notification with the server's message — local values stay as edited, no retry loop.

#### Scenario: Offline edit retried at next upload
- **WHEN** a bag edit's PATCH fails with a network error and a shot is later uploaded
- **THEN** the upload cycle SHALL re-send the bag PATCH and clear `visualizerSyncPending` on 200

#### Scenario: Offline edit retried without a shot upload
- **WHEN** a bag edit's PATCH fails with a network error and an edit-pull pass later completes
- **THEN** the bag PATCH is re-sent and `visualizerSyncPending` cleared on 200

#### Scenario: Rename collides with an existing remote bag
- **WHEN** the push returns 422 for a name+roast_date collision
- **THEN** the local edit SHALL be kept, the flag cleared, and the user notified once — no repeated retries

#### Scenario: Premium lapsed
- **WHEN** the push returns 403
- **THEN** the flag SHALL be cleared and CM state cached as `NO_COFFEE_MANAGEMENT`, suppressing further edit-time pushes until a connection test resets the state
