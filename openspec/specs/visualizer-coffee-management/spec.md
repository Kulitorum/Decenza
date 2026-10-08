# visualizer-coffee-management Specification

## Purpose
Defines how Decenza detects whether a user's Visualizer account has Coffee Management (CM) enabled via a probe PATCH, and — only while CM is active — find-or-creates the matching remote roaster and coffee bag, links each uploaded shot to it, and keeps local bag edits pushed to the linked Visualizer bag, all idempotently and without disturbing server-owned CM enable/disable lifecycle.

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
- **AND** only POST a new bag when no match exists, with the verified field names: `name`, `roaster_id`, `roast_date`, `roast_level`, `country`, `region`, `farm`, `farmer` (our producer), `variety`, `processing`, `harvest_time`, `quality_score`, `place_of_purchase`, `tasting_notes`, `elevation`, `url` (our link), `frozen_date`, `defrosted_date` (server name for our defrostDate), `notes`, `canonical_coffee_bag_id`
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

### Requirement: Bag edits auto-push to the linked Visualizer bag

A bag save changing a mapped field SHALL send `PATCH
/api/coffee_bags/{visualizerBagId}` when the bag has a `visualizerBagId` and
Visualizer credentials exist. The PATCH SHALL carry only mapped fields whose
local value differs from the value last seen on Visualizer
(`coffee_bags.visualizer_seen`). After a 200, the sent values SHALL be recorded
as seen. With nothing to send, including no roaster or archive change, no PATCH
SHALL be sent.

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

### Requirement: Cleared and never-seen bag fields

A field cleared locally SHALL be sent as `null`, except the name and the
canonical link, which SHALL NOT be sent as `null`. A field Visualizer has never
been seen to hold SHALL be sent only when it is set locally, so a bag that
predates this tracking never wipes a server-side value.

#### Scenario: Never-seen field is not wiped

- **WHEN** a bag that predates this tracking has an empty region and Visualizer holds a region
- **THEN** no region is sent

### Requirement: Bag field mapping for identity and dates

The identity and date fields SHALL map as: `coffeeName` to `name`, `roastDate`
to `roast_date`, `roastLevel` to `roast_level`, `frozenDate` to `frozen_date`,
`defrostDate` to `defrosted_date`, `notes` to `notes` (as HTML), and
`beanBaseId` to `canonical_coffee_bag_id`.

#### Scenario: Identity and date fields reach Visualizer

- **WHEN** a linked bag's frozen date and defrost date change in Decenza
- **THEN** the PATCH carries `frozen_date` and `defrosted_date` with the new values

### Requirement: Bag field mapping for descriptive fields

The descriptive fields SHALL map as: `origin` to `country`, `region` to
`region`, `farm` to `farm`, `producer` to `farmer`, `variety` to `variety`,
`elevation` to `elevation`, `process` to `processing`, `harvest` to
`harvest_time`, `qualityScore` to `quality_score`, `placeOfPurchase` to
`place_of_purchase`, `tastingNotes` to `tasting_notes`, and `link` to `url`.

#### Scenario: Descriptive fields reach Visualizer

- **WHEN** a linked bag's origin and process change in Decenza
- **THEN** the PATCH carries `country` and `processing` with the new values

### Requirement: Roaster rename and dose or grind writes

A roaster name change SHALL re-resolve the roaster and re-point `roaster_id`
when it changed. Dose and grind write-throughs SHALL NOT trigger a push, because
they are not Visualizer-stored fields (the shipped `touchesVisualizerFields`
gate).

#### Scenario: Roaster rename re-points the roaster

- **WHEN** the roaster name of a linked bag changes
- **THEN** the roaster is re-resolved and `roaster_id` is re-pointed when the roaster changed

### Requirement: Edit-push failure handling

A retryable push failure (network error, 429, 5xx) SHALL set the bag's
`visualizerSyncPending` flag. The bag SHALL be re-pushed after the next shot
upload and at the end of each edit-pull pass, sending only the fields that still
differ, and the flag SHALL be cleared on success. Retries are event-driven, with
no timer.

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

### Requirement: Bag push 403 and 404 responses

A 403 SHALL clear the flag and cache CM state as `NO_COFFEE_MANAGEMENT`,
suppressing edit-time pushes until a connection test resets it. A 404 SHALL
clear the flag, since the remote id is stale and the next shot upload re-creates
and re-links the bag.

#### Scenario: Stale remote id is cleared

- **WHEN** a bag push returns 404
- **THEN** the flag is cleared and the next shot upload re-creates and re-links the bag

### Requirement: A 422 is reported once and not retried

A 422 (for example a name and roast-date collision, or defrost before frozen)
SHALL clear the flag and surface a non-blocking notification with the server's
message. The local values SHALL stay as edited, with no retry loop.

#### Scenario: Validation refusal is shown once

- **WHEN** a bag push returns 422
- **THEN** the notification shows the server message once, the local values are kept, and no further retry is made

### Requirement: Shot endpoints of record

`GET /api/shots/{id}` SHALL be treated as returning `coffee_bag_id` only when
the shot has a bag. A bag-less shot OMITS the key, because the hash is
compacted, so absent and null both mean no bag. A present value that is not a
string SHALL NOT be read as no bag.

#### Scenario: A bag-less shot omits the key

- **WHEN** a shot with no server-side coffee bag is read
- **THEN** `coffee_bag_id` SHALL be absent from the response
- **AND** the system SHALL treat that as "no bag"

#### Scenario: An unreadable coffee_bag_id is not "no bag"

- **WHEN** `coffee_bag_id` is present but is not a string
- **THEN** the system SHALL treat the response as unusable and leave the shot queued, rather than acting on an assumed absence

### Requirement: No read confirms an unlink

`canonical_coffee_bag_id` SHALL NOT be expected in any shot response, so no read
can confirm that an unlink took effect.

#### Scenario: Unlink cannot be read back

- **WHEN** an unlink is sent for a shot
- **THEN** no subsequent shot read is treated as confirming the unlink

### Requirement: The shot list is not used for bean identity

`GET /api/shots` (the list) SHALL NOT be used to answer any question about a
shot's bean identity, because it renders only `clock`, `id` and `updated_at`.

#### Scenario: List absence is not an empty bean

- **WHEN** a shot is read from the list
- **THEN** its missing bean fields are not treated as empty values

### Requirement: Shot requests are paced against published limits

Requests SHALL be paced against the published limits of 50 per minute and 200
per 10 minutes per IP and per user. Shot upload shares that budget, so an
unpaced background pass can rate-limit a user's espresso uploads.

#### Scenario: Background pass stays within the limits

- **WHEN** a background pass runs alongside shot uploads
- **THEN** combined requests stay within the published limits

### Requirement: Borrowed-record shot repair is a recorded queue, not a library scan

Shots renamed by a borrowed canonical record SHALL be repaired only from the
flag recorded at the unlink, never by comparing the local library against the
server. The pass SHALL read each queued shot before writing anything.

#### Scenario: Only flagged shots are considered

- **WHEN** a repair pass runs
- **THEN** it SHALL read only shots flagged at an unlink
- **AND** a shot the server already agrees with SHALL settle without any write

#### Scenario: A failed read leaves the shot queued

- **WHEN** a shot's read fails, or returns a body that cannot be parsed or lacks the bean fields
- **THEN** no write SHALL be made for that shot and its flag SHALL remain set

### Requirement: Repair writes only on a real difference

The pass SHALL write only on a real difference, compared trim- and case-
insensitively.

#### Scenario: Case or whitespace difference is not written

- **WHEN** the server and local bean names differ only by case or surrounding whitespace
- **THEN** no write is made for that shot

### Requirement: Only a server-confirmed shot clears its flag

A shot's flag SHALL be cleared only when the server has confirmed that shot
needs nothing further. Nothing else SHALL clear a flag, so an interrupted or
failed pass resumes later.

#### Scenario: Interrupted pass resumes on a later launch

- **WHEN** a repair pass is interrupted before a queued shot is confirmed
- **THEN** that shot stays flagged and a later launch resumes it

### Requirement: What the repair may write to a shot

Before writing, the pass SHALL decide per shot from the read. When the shot has
a server-side coffee bag, the pass SHALL write nothing, because such a shot
takes its identity from that bag on every touch.

#### Scenario: A shot with a server bag is declined

- **WHEN** a queued shot is read and has a `coffee_bag_id`
- **THEN** no request SHALL be sent for it
- **AND** its flag SHALL be cleared, since no future pass could do more

#### Scenario: Incomplete local names never blank the server's

- **WHEN** a queued shot's local bean brand or bean type is empty and the shot has no server bag
- **THEN** the request SHALL clear the canonical link and SHALL NOT contain the bean names

#### Scenario: A discarded write is not counted as a repair

- **WHEN** a write returns success but the response shows the server kept different bean names
- **THEN** the outcome SHALL be reported as not applied
- **AND** the pass SHALL NOT claim the shot was corrected

### Requirement: Empty local bean names never blank the server

When the shot's local bean brand or bean type is empty, the pass SHALL NOT send
bean names, since an empty name blanks a real value on the user's account. It
SHALL clear the borrowed link alone, which needs no names.

#### Scenario: Empty local name clears only the link

- **WHEN** a queued shot has no server bag and its local bean brand is empty
- **THEN** the request clears the canonical link and carries no bean names

### Requirement: A repair sends names with an explicit link clear

Otherwise the pass SHALL send the local bean names together with an explicit
clear of the canonical link.

#### Scenario: Full repair sends names and clears the link

- **WHEN** a queued shot has no server bag and its local bean brand and type are set
- **THEN** the request carries the bean names and clears the canonical link

### Requirement: A success status is not proof the write took

An HTTP success status SHALL NOT be taken as proof that the values took, because
the server may overwrite them from a bag between the request and the save. The
pass SHALL read the response back and report a write the server discarded,
rather than counting it as a repair.

#### Scenario: Read-back confirms the write

- **WHEN** a write returns success and the response shows the server kept the names
- **THEN** the shot is counted as repaired

### Requirement: Repair pass failure vocabulary

A refusal that cannot differ per shot SHALL abandon the pass rather than repeat
itself against every remaining shot; the flags keep the remainder for a later
launch. Rate limiting and an invalid credential are such refusals.

#### Scenario: Rate limiting stops the pass

- **WHEN** the server rate-limits a request
- **THEN** the pass SHALL stop
- **AND** every shot not yet settled SHALL remain flagged for a later launch

#### Scenario: An authorization refusal on the read stops the pass

- **WHEN** a read is refused as unauthorized
- **THEN** the pass SHALL stop rather than settle that shot, because the read path authorizes nothing per shot

#### Scenario: An authorization refusal on the write settles one shot

- **WHEN** a write is refused as unauthorized or the shot is gone
- **THEN** that shot alone SHALL be settled and the pass SHALL continue

### Requirement: An authorization refusal on the read is account-wide

An authorization refusal on a read SHALL abandon the pass, because the read path
authorizes nothing per shot, so the refusal is account- or network-wide.

#### Scenario: Read refusal does not settle shots

- **WHEN** a read is refused as unauthorized
- **THEN** no shot is settled on the strength of that refusal

### Requirement: A write refusal naming one shot settles that shot

A refusal on a write that names one shot, because the shot is not this account's
or no longer exists, SHALL settle that shot alone and continue, since it can
never succeed.

#### Scenario: Shot-specific write refusal is settled

- **WHEN** a write is refused because the shot is not this account's or no longer exists
- **THEN** that shot is settled and the pass continues with the next shot

### Requirement: A pass reports incomplete only when work is left

A pass SHALL report itself incomplete only when work was genuinely left queued.
It SHALL distinguish shots whose names were restored, shots whose link was
cleared, and shots it declined to touch.

#### Scenario: Outcome reports each category

- **WHEN** a repair pass finishes
- **THEN** the report counts restored names, cleared links and declined shots separately

### Requirement: A bag edit is pushed without waiting for a shot upload

A bag edit that changes a Visualizer-mapped field, from the bag editor, an AI
fill or MCP `bag_update`, SHALL be pushed to the linked Visualizer bag at once,
unless Coffee Management is known to be off. It SHALL NOT wait for a shot upload
to confirm Coffee Management.

#### Scenario: AI fill on a device that does not upload shots
- **GIVEN** the desktop app, with no shot uploaded this session
- **WHEN** the user fills a synced bag's details with AI and saves
- **THEN** the bag's fields reach Visualizer without any shot being uploaded

### Requirement: Unconfirmed CM does not create a roaster

While Coffee Management is unconfirmed, a roaster rename SHALL re-point only to
an existing roaster and SHALL NOT create one.

#### Scenario: Rename with unconfirmed CM creates no roaster

- **WHEN** a roaster is renamed while Coffee Management is unconfirmed and no existing roaster matches
- **THEN** no roaster is created

### Requirement: A failed bag push is retried later

A bag whose push failed SHALL be retried after a shot upload and at the end of
each edit-pull pass.

#### Scenario: Failed push is retried after a pass

- **WHEN** a bag push failed and an edit-pull pass completes
- **THEN** the bag push is sent again
