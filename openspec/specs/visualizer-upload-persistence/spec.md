# visualizer-upload-persistence Specification

## Purpose
Defines the authoritative, UI-independent path by which a successful Visualizer upload's returned shot id and URL are written back to the originating local `shots` row from `MainController`, plus the one-time bounded reconciliation pass that relinks pre-existing uploads missing that id and corrects stale cloud ratings, and the upload payload's grinder, rpm, and coffee_bag_id resolution rules.
## Requirements
### Requirement: A successful Visualizer upload SHALL persist its returned id to the originating local shot via a non-UI path

When a Visualizer upload succeeds and returns a shot id, the application SHALL persist that id and its shot URL to the local `shots` row of the shot that was uploaded, through a code path that does NOT depend on any QML page being instantiated, visible, or still alive. The persistence SHALL be driven from `MainController` (C++), correlating the result to the originating shot by an explicit DB shot id threaded through the uploader — not by reading mutable shared state at signal time, and not by any timer or delay.

There SHALL be exactly one authoritative writeback path. The pre-existing QML `onUploadSuccess` persistence calls in `PostShotReviewPage` and `ShotDetailPage` SHALL no longer perform the DB write (they may retain pure UI refresh on `visualizerInfoUpdated`).

#### Scenario: Auto-upload with the post-shot review page disabled still records the id

- **GIVEN** `visualizer/showAfterShot` is `false` (post-shot review page never shown)
- **AND** auto-upload is enabled with valid credentials
- **WHEN** a shot is pulled, saved as local row N, and the upload completes successfully returning Visualizer id `V`
- **THEN** local shot row N SHALL have `visualizer_id == V` and a non-empty `visualizer_url`
- **AND** `hasVisualizerUpload` for row N SHALL be `true`

#### Scenario: Post-shot review page dismissed before the upload completes

- **GIVEN** the post-shot review page is shown then dismissed (or auto-closes) before the ~1 s upload round-trip resolves
- **WHEN** the upload completes successfully for local row N returning id `V`
- **THEN** row N SHALL have `visualizer_id == V` (persistence does not depend on the page surviving)

#### Scenario: History re-upload correlates to the re-uploaded shot, not the last-saved shot

- **GIVEN** the user re-uploads an old history shot row M while the most recently pulled shot is a different row L
- **WHEN** that upload succeeds returning id `V`
- **THEN** `visualizer_id == V` SHALL be written to row M (the re-uploaded shot), and row L SHALL be unaffected

#### Scenario: Upload failure persists nothing

- **WHEN** an upload fails (network error, 4xx/5xx, or no id in the response)
- **THEN** no `visualizer_id` SHALL be written for the originating shot
- **AND** the originating shot SHALL remain eligible for the reconciliation pass

### Requirement: A one-time bounded reconciliation SHALL relink already-uploaded-but-unrecorded shots and correct stale cloud ratings

The application SHALL run, at most once per device (guarded by an internal QSettings run-once flag), a reconciliation pass that lists the user's shots from the Visualizer API and links them to local shot rows that were uploaded before the authoritative writeback existed. The pass SHALL:

- run only when Visualizer credentials are present; when absent it SHALL skip WITHOUT setting the run-once flag (so it retries on a later boot once configured);
- be bounded to a recent time window (it SHALL NOT page through the user's entire cloud history);
- consider only local rows whose `visualizer_id` is empty and whose shot timestamp is within the window;
- match a local row to a Visualizer shot by shot start time within a tight tolerance (≤ 2 s), 1:1 — a Visualizer shot already linked to any local row, or already consumed in this pass, SHALL NOT be reused, and an ambiguous match SHALL be skipped (not guessed);
- for each linked row, persist `visualizer_id`/`visualizer_url`, then push the local rating to Visualizer via the existing update path (which sends a cleared rating as JSON `null`). The push SHALL be unconditional per linked row — the Visualizer list endpoint does not return the cloud rating, and an unconditional idempotent PATCH over the bounded orphan set is preferred to an extra per-shot detail fetch;
- run off the main thread for both the network call and the DB writes;
- set the run-once flag only after a fully completed pass; a pass aborted by a network/parse error SHALL NOT set the flag, and any links already written SHALL be idempotent on the next attempt.

This reconciliation SHALL be functionally independent of the migration-16 inferred-rating back-sync (change `remove-inferred-shot-ratings`): it SHALL fully repair an orphaned-and-stale shot on its own, in any boot order, without relying on that migration's queue.

#### Scenario: Orphaned upload is relinked by timestamp

- **GIVEN** local row N has empty `visualizer_id`, timestamp `T` (within the window)
- **AND** the user's Visualizer library contains a shot with id `V` whose start time is within 2 s of `T`, not linked to any local row
- **WHEN** the reconciliation pass runs with valid credentials
- **THEN** row N SHALL be linked: `visualizer_id == V`, `visualizer_url` set

#### Scenario: Relink also corrects a stranded rating

- **GIVEN** the conditions above, AND local row N's `enjoyment` is `0` (the corrected value after migration 16) while the cloud shot `V` still carries `espresso_enjoyment == 75`
- **WHEN** the reconciliation links row N
- **THEN** an update SHALL be sent to Visualizer for `V` carrying row N's local rating — here clearing it (JSON `null`)
- **AND** this SHALL occur regardless of whether the migration-16 back-sync ran, and regardless of the cloud's prior value (the push is unconditional per linked row)

#### Scenario: Idempotent re-run is a no-op

- **GIVEN** the reconciliation completed and set its run-once flag
- **WHEN** the application boots again
- **THEN** the pass SHALL NOT run, and no Visualizer list call SHALL be made

#### Scenario: Missing credentials defers, does not consume the run-once

- **GIVEN** no Visualizer credentials are configured
- **WHEN** boot reaches the reconciliation trigger
- **THEN** the pass SHALL skip and the run-once flag SHALL remain unset
- **AND** a later boot with credentials configured SHALL run it

#### Scenario: No false reuse of an already-linked cloud shot

- **GIVEN** cloud shot `V` is already recorded as `visualizer_id` on local row A
- **WHEN** the reconciliation evaluates a different empty-id row B whose timestamp is also within tolerance of `V`'s start time
- **THEN** `V` SHALL NOT be linked to B (no reuse); B is left for manual handling

### Requirement: Visualizer upload SHALL resolve grinder identity via the equipment package
The Visualizer upload payload builders in `src/network/visualizeruploader.cpp` SHALL resolve grinder brand/model by following the shot's `equipment_id` to the package's grinder item, then combine them into the single `grinder_model` string Visualizer expects (`"brand model"`). The payload shape SHALL be otherwise unchanged: `grinder_model`, `grinder_setting`, `grinder_dose_weight` (and their `meta.grinder.*` / `settings.*` mirrors). Burrs SHALL remain unsent (Visualizer has no field for it).

#### Scenario: Pointer resolved on upload
- **WHEN** a shot with a linked equipment package is uploaded
- **THEN** `grinder_model` SHALL contain the resolved "brand model" string from the package's grinder item

#### Scenario: Shot with no linked equipment
- **WHEN** a shot has a null `equipment_id`
- **THEN** the grinder fields SHALL be omitted from the payload rather than sent empty

### Requirement: Visualizer upload SHALL append rpm to the grind setting
Because Visualizer has no rpm field, when a shot has an rpm dial-in value the uploader SHALL append it to the `grinder_setting` string using the community convention (`"{setting} {rpm}rpm"`, e.g. `"2.4 1400rpm"`). When rpm is absent the `grinder_setting` SHALL be sent unmodified.

#### Scenario: rpm appended
- **WHEN** a shot has `grinder_setting = "2.4"` and `rpm = 1400`
- **THEN** the uploaded `grinder_setting` SHALL be `"2.4 1400rpm"`

#### Scenario: no rpm
- **WHEN** a shot has `grinder_setting = "2.4"` and no rpm
- **THEN** the uploaded `grinder_setting` SHALL be `"2.4"`

### Requirement: Visualizer profile import SHALL remain unchanged
Visualizer import (`src/network/visualizerimporter.cpp`) imports profiles only; the `grinder_model`/`grinder_setting` it reads are display-only metadata never persisted to a local shot or bag. This change SHALL NOT add equipment-package creation or `equipment_id` linkage on the import path.

#### Scenario: Import does not create packages
- **WHEN** a user imports a shared shot's profile from Visualizer
- **THEN** no equipment package SHALL be created and no local shot/bag SHALL be linked

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

### Requirement: Shot sampling SHALL capture the machine's mix temperature setpoint

The DE1 `ShotSample` notification carries two temperature setpoints: `SetMixTemp` (the target for water entering the group) and `SetHeadTemp` (the target for the basket). Decenza SHALL decode both and retain them as distinct per-sample series for the duration of a shot.

#### Scenario: New BLE spec packet decodes both setpoints
- **WHEN** a 19-byte `ShotSample` notification is received
- **THEN** `SetMixTemp` SHALL be decoded from bytes 11–12 as an unsigned big-endian short divided by 256
- **AND** `SetHeadTemp` SHALL continue to be decoded from bytes 13–14
- **AND** the two values SHALL be exposed as separate fields, neither overwriting the other

#### Scenario: Old BLE spec packet decodes both setpoints
- **WHEN** a 17-byte `ShotSample` notification is received
- **THEN** `SetMixTemp` SHALL be decoded from bytes 8–9
- **AND** `SetHeadTemp` SHALL continue to be decoded from bytes 10–11

#### Scenario: Existing temperature goal semantics are preserved
- **WHEN** any `ShotSample` is decoded
- **THEN** the field feeding `temperature.goal` on upload SHALL remain `SetHeadTemp`, matching de1app's `espresso_temperature_goal` and Visualizer's "Basket Temperature Goal" series

### Requirement: The mix temperature goal SHALL persist with the shot

The mix temperature goal series SHALL survive a save/load round-trip so that re-uploads, queued uploads, and the shot detail graph all see the same data the live shot did.

#### Scenario: Series round-trips through storage
- **WHEN** a shot with mix goal samples is saved and then loaded from shot history
- **THEN** the loaded record SHALL carry a mix goal series equal to the one recorded

#### Scenario: Shots recorded before this change load without error
- **WHEN** a shot whose stored sample blob has no mix goal key is loaded
- **THEN** the load SHALL succeed
- **AND** the mix goal series SHALL be empty
- **AND** no other series SHALL be affected

### Requirement: Visualizer upload SHALL include the mix temperature goal when available

Uploaded shot JSON SHALL carry `temperature.mix_goal` alongside `temperature.goal`, interpolated onto the elapsed timeline using the same interpolation applied to every other goal series. Both the live upload path and the history/re-upload path SHALL produce it.

#### Scenario: Live upload includes mix_goal
- **WHEN** a shot recorded with mix goal data is uploaded to Visualizer
- **THEN** `temperature.mix_goal` SHALL be present
- **AND** it SHALL have the same length as `elapsed`
- **AND** `temperature.goal` SHALL still carry the basket (`SetHeadTemp`) goal

#### Scenario: Re-upload from history includes mix_goal
- **WHEN** a stored shot carrying mix goal data is re-uploaded from shot history
- **THEN** its JSON SHALL contain `temperature.mix_goal` with the same values the live upload would have sent

#### Scenario: Shots without mix goal data omit the key entirely
- **WHEN** a shot with an empty mix goal series (recorded before this change, or imported from a `.shot` file) is uploaded
- **THEN** `temperature.mix_goal` SHALL be absent from the JSON
- **AND** the upload SHALL NOT send a zero-filled array

#### Scenario: History upload path matches the live path for mix temperature
- **WHEN** a stored shot carrying measured mix temperature data is re-uploaded from history
- **THEN** `temperature.mix` SHALL be present, as it is on the live upload path

