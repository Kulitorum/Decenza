# visualizer-upload-persistence Specification

## Purpose
Defines the authoritative, UI-independent path by which a successful Visualizer upload's returned shot id and URL are written back to the originating local `shots` row from `MainController`, plus the one-time bounded reconciliation pass that relinks pre-existing uploads missing that id and corrects stale cloud ratings, and the upload payload's grinder, rpm, and coffee_bag_id resolution rules.

## Requirements

### Requirement: A successful Visualizer upload SHALL persist its returned id to the originating local shot via a non-UI path

A successful Visualizer upload that returns a shot id SHALL persist that id and
its shot URL to the local `shots` row of the uploaded shot. The write SHALL be
driven from `MainController` (C++) and SHALL NOT depend on any QML page being
instantiated, visible or alive. The result SHALL be correlated by an explicit DB
shot id threaded through the uploader, not by reading shared state at signal
time. There SHALL be one authoritative writeback path.

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

### Requirement: QML pages do not write the Visualizer id

The QML `onUploadSuccess` handlers in `PostShotReviewPage` and `ShotDetailPage`
SHALL NOT perform the DB write. They MAY keep a pure UI refresh on
`visualizerInfoUpdated`.

#### Scenario: Page absent at completion does not lose the id

- **GIVEN** the post-shot review page is not on screen when the upload completes
- **THEN** the id is still written to the originating local row

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

### Requirement: Barista and notes reach Visualizer as written

The upload SHALL carry the barista as `app.data.settings.my_name`, the key Visualizer's parser reads. Notes SHALL be sent Markdown-escaped on upload (Visualizer renders them as Markdown) and as HTML paragraphs and line breaks on a shot or bag PATCH (Visualizer reads them as HTML). Notes read from Visualizer, including by shot recovery, SHALL be converted from HTML to plain text. A request Visualizer refuses with a JSON `error` SHALL show that message.

#### Scenario: Multi-line note edited
- **WHEN** the user edits a note to two lines and the update is sent
- **THEN** Visualizer shows the note on two lines

#### Scenario: Barista uploaded
- **WHEN** a shot with a barista is uploaded
- **THEN** the Visualizer shot shows that barista

#### Scenario: Disabled account
- **WHEN** Visualizer refuses an upload with 403 and a JSON error
- **THEN** the upload status shows Visualizer's message rather than "HTTP 403"
