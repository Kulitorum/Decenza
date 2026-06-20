# visualizer-coffee-management Specification

## Purpose
TBD - created by archiving change bean-bag-inventory. Update Purpose after archive.
## Requirements
### Requirement: Coffee Management capability detection via probe PATCH
The system SHALL detect the CM state with a single-field probe PATCH against the app's
own just-uploaded shot: body `{"shot": {"coffee_bag_id": "<id>"}}` with
`Accept: application/json`.

#### Scenario: Probe outcomes
- **WHEN** the probe PATCH returns 200
- **THEN** CM state SHALL be cached as `COFFEE_MANAGEMENT_ACTIVE` (the link landed; the server refreshed the shot's bean fields from the bag)
- **WHEN** the probe PATCH returns 400
- **THEN** CM state SHALL be cached as `PREMIUM_NO_CM` (`coffee_bag_id` not permitted — Coffee Management is off for this account)
- **WHEN** the probe returns 401, 429, a 5xx, or a network error
- **THEN** CM state SHALL remain `UNKNOWN` and the probe SHALL be retried on a later upload — transient failures never cache a negative state

#### Scenario: Probe bag id source
- **WHEN** the probe needs a bag id and `GET /api/coffee_bags` returns any bag
- **THEN** the shot's own (find-or-created) bag SHALL be used when available, else any existing bag id — no throwaway objects are created for detection
- **WHEN** the user has zero bags
- **THEN** the system SHALL create the real bag first (see bag creation below); a 403 on that POST SHALL cache `NO_COFFEE_MANAGEMENT` (not premium — bag CRUD is premium-gated)

#### Scenario: CM state re-evaluated on connection test
- **WHEN** the user runs a connection test in credentials settings
- **THEN** the cached CM state SHALL be reset to `UNKNOWN` and re-detected on the next upload cycle

### Requirement: Visualizer bag creation at upload time (CM active only)
When CM state is `COFFEE_MANAGEMENT_ACTIVE` and a shot is uploaded with a linked local bag that has no `visualizerBagId`, the system SHALL find-or-create the remote roaster and bag, then link the shot.

#### Scenario: Roaster resolution order
- **WHEN** a remote bag must be created
- **THEN** the roaster SHALL be resolved: `beanBaseData.canonicalRoasterId` passed as `canonical_roaster_id` on creation when present; an existing roaster matched by name via GET /api/roasters; else POST /api/roasters `{name}` — storing the id in `bag.visualizerRoasterId`

#### Scenario: Bag find-or-create
- **WHEN** the roaster id is known
- **THEN** the system SHALL first look for an existing remote bag matching name + roast_date in `GET /api/coffee_bags?roaster_id=<id>` (the server's own CM-enable job dedupes on roaster+name+roast_date; the API create endpoint does not)
- **AND** only POST a new bag when no match exists, with the verified field names: `name`, `roaster_id`, `roast_date`, `roast_level`, `country`, `region`, `variety`, `processing`, `harvest_time`, `tasting_notes`, `elevation`, `frozen_date`, `defrosted_date` (server name for our defrostDate), `notes`, `canonical_coffee_bag_id`
- **AND** store the returned UUID in `bag.visualizerBagId`
- **AND** `startWeightG` SHALL NOT be sent (no server field; the server's free-form `metadata` belongs to the user)

#### Scenario: Shot link via post-upload PATCH
- **WHEN** the upload POST succeeds and CM is active with a known `visualizerBagId`
- **THEN** the system SHALL PATCH the shot with `coffee_bag_id` (and `canonical_coffee_bag_id` when linked — always accepted) using `Accept: application/json` and the `{"shot": {...}}` body — the upload POST itself ignores `coffee_bag_id` (verified)

### Requirement: No remote bag creation when CM is off
When CM state is `PREMIUM_NO_CM`, `NO_COFFEE_MANAGEMENT`, or `UNKNOWN` (beyond the probe itself), the system SHALL NOT create remote bags or roasters. Shot writes carry only `canonical_coffee_bag_id` (today's behavior). A CM-off user's remote bag list is dormant server state they never see — adding to it is clutter.

#### Scenario: CM-off upload
- **WHEN** a shot uploads while CM state is `PREMIUM_NO_CM`
- **THEN** no POST to /api/coffee_bags or /api/roasters SHALL occur
- **AND** the metadata PATCH SHALL include `canonical_coffee_bag_id` only

### Requirement: Bag sync is idempotent across upload cycles
Once `visualizerBagId` is stored locally, no upload cycle SHALL POST another remote bag
for that coffee; the find-before-create lookup additionally protects against
duplicates when local state was lost (e.g. restored backup).

#### Scenario: Next upload cycle after partial failure
- **WHEN** a remote bag was created but the shot PATCH failed
- **THEN** the next upload cycle SHALL reuse the stored `visualizerBagId` and only retry the PATCH

### Requirement: Server-side CM lifecycle is respected
The server owns mass link/unlink: enabling CM auto-creates bags from the user's shot history and links shots; disabling CM unlinks all shots but keeps bags. The system SHALL NOT attempt to repair or mirror those transitions — the probe simply re-detects the current state on the next upload after a connection test.

#### Scenario: User toggles CM on Visualizer
- **WHEN** the user enables or disables CM on visualizer.coffee and later runs a connection test (or the next probe-triggering upload occurs after a cached-negative state is reset)
- **THEN** the system SHALL converge on the new state via the probe without any bulk re-linking of historical shots

