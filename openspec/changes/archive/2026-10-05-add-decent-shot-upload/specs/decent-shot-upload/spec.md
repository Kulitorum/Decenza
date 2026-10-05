## Purpose

Uploads the user's espresso shots to their linked Decent account, so they appear in the account's shot history alongside shots from de1app and Decaid. Covers the payload, new shots, edited shots, the history backlog, and the per-shot record of what was sent.

## ADDED Requirements

### Requirement: Decent switch and shared upload settings

Uploading to Decent SHALL have its own on/off switch, off until an account is linked; linking switches it on, and the user can switch it off. Automatic behaviour SHALL follow the Shot Upload tab's shared Upload settings ("Auto-upload shots", "Auto-update shots", "Minimum Duration"), which also govern Visualizer; Decent SHALL NOT have separate copies of them. No shot SHALL be sent to Decent while its switch is off or no account is linked. Turning the switch on SHALL NOT send saved shots by itself; they are offered by Upload missing shots.

#### Scenario: Linked but switched off
- **WHEN** a user links a Decent account and then switches Decent off
- **THEN** no shot is uploaded to Decent, automatically or from the review page

#### Scenario: Switching Decent off
- **WHEN** the user switches Decent off
- **THEN** no further Decent upload is started, including an Upload missing shots run

#### Scenario: One minimum length
- **WHEN** the shared minimum shot length is 6 seconds and both destinations are on
- **THEN** a 5-second shot is uploaded to neither

### Requirement: Shot eligibility

A shot SHALL be uploaded automatically only when all of these hold: it is a saved shot from history whose beverage type is not a maintenance type (cleaning, descaling); its duration is at least the shared minimum shot length; a machine serial number is available (see the machine identity requirement); it has not already been uploaded; and it has not been permanently rejected. A shot that fails any condition SHALL be skipped without an error.

#### Scenario: Flush-length shot
- **WHEN** a 3-second shot is saved with the shared minimum length at 6 seconds
- **THEN** it is not uploaded

#### Scenario: Simulator shot
- **WHEN** a shot pulled in simulation mode is uploaded
- **THEN** it is sent with serial `SIM-DE1`, the server answers 403, and the user is told that serial is not registered to their account

### Requirement: Machine identity in the payload

Every uploaded document SHALL carry `machine.serialNumber`, and SHALL carry `machine.firmwareVersion` and `machine.model` when known. For a first upload these SHALL come from the DE1 connected at the time of upload; the serial is not stored with the shot itself. A replacement of an already-uploaded shot SHALL reuse the serial the shot was first uploaded under, so it lands on the same machine in the account. If no DE1 is connected when a first upload is due, the shot SHALL NOT be sent or marked rejected, and stays offered by Upload missing shots. In simulation mode the machine reports the fixed serial `SIM-DE1`. It is not a number, so no Decent account can own it, and the server refuses its uploads (403): the simulator exercises sign-in, the request and the response handling, and with `SIM-DE1` no simulated shot can reach an account. For testing a full upload, MCP `settings_set` `simulatorSerialNumber` SHALL make the simulator report a given serial until the app restarts. It SHALL never be saved and SHALL have no on-screen control; an empty value restores `SIM-DE1`.

#### Scenario: Shot uploaded right after it is pulled
- **WHEN** an eligible shot is saved while a real DE1 is connected
- **THEN** it is uploaded with that machine's serial as `machine.serialNumber`

#### Scenario: Simulator with a test serial
- **WHEN** a developer sets `simulatorSerialNumber` to their machine's serial over MCP and uploads a simulated shot
- **THEN** it is sent under that serial, and after the app restarts the simulator reports `SIM-DE1` again

#### Scenario: No machine connected
- **WHEN** a never-uploaded shot is due and no real DE1 is connected
- **THEN** it is not sent and remains offered by Upload missing shots

#### Scenario: Replacement after switching machines
- **WHEN** a shot first uploaded under serial A is edited while a machine with serial B is connected
- **THEN** the replacement is sent with serial A

### Requirement: Payload is a Decaid ShotRecord document

Each upload SHALL be a `POST` to `https://decentespresso.com/support/api/shot_upload` with header `Content-Type: application/json`, exactly as Decaid and Decent's API documentation send it. The body SHALL be a single Decaid `ShotRecord` JSON document, UTF-8 encoded exactly once, containing:
- `id`: a stable shot identifier, the same on every upload of that shot, which does not collide with shots from other devices or apps;
- `timestamp`: shot start in ISO 8601 UTC with milliseconds;
- `measurements`: one entry per sample, each with `machine` (`timestamp`, `state`, `flow`, `pressure`, `targetFlow`, `targetPressure`, `mixTemperature`, `groupTemperature`, `targetMixTemperature`, `targetGroupTemperature`, `profileFrame`) and, when a scale was in use, `scale` (`timestamp`, `weight`, `weightFlow`);
- `workflow`: `name` (profile title), `profile` (the profile the shot ran, in de1app v2 JSON), and `context` (`targetDoseWeight`, `targetYield`, `grinderModel`, `grinderSetting`, `coffeeName`, `coffeeRoaster`, `baristaName`, `finalBeverageType`, and `extras.roastDate` as ISO `yyyy-mm-dd` plus `extras.roastLevel` and `extras.grinderRpm`);
- `annotations`: `actualDoseWeight`, `actualYield`, `drinkTds`, `drinkEy`, `enjoyment` (0-100), `espressoNotes`;
- `machine`: as in the machine identity requirement;
- `app`: `{"name":"decenza","version":<app version>,"sourceFormat":"decenza"}`;
- `schemaVersion`: `1`.

Fields with no recorded value SHALL be omitted rather than sent as empty strings or zeros. A sample with no recorded value for a channel inside the time series SHALL carry `0`, so the parallel series stay aligned (as de1app's converter does).

#### Scenario: Server accepts the document
- **WHEN** a valid shot is uploaded
- **THEN** the server responds 2xx with `"ok":true`
- **AND** the stored shot shows the same profile title, bean, grinder, dose, yield, rating and notes as the local shot

#### Scenario: Accented text
- **WHEN** a shot's bean name contains non-ASCII characters (e.g. "Café Allongé")
- **THEN** the stored shot shows the identical characters, not mojibake

#### Scenario: Unknown roast date
- **WHEN** the shot's roast date is empty or is not a valid date
- **THEN** `extras.roastDate` is absent from the document

### Requirement: New shots upload once they are saved

With the Decent switch on and the shared "Auto-upload shots" setting on, the system SHALL upload each eligible shot after it has been saved to history, using the saved row as the source of truth, through the shared upload path (shot-uploads). Uploads SHALL be sent one at a time: a shot saved while another upload or a backlog batch is in flight SHALL be queued and sent next.

#### Scenario: Shot finishes
- **WHEN** an eligible espresso shot is saved
- **THEN** it is uploaded and its upload is recorded on the shot

#### Scenario: Two shots back to back while offline
- **WHEN** two shots are saved while the Decent server is unreachable
- **THEN** both are recorded as failed and offered by Upload missing shots, and neither is marked rejected

### Requirement: Edited shots are re-uploaded with replace

When the metadata of a shot that has already been uploaded changes — from the post-shot review (once, when it closes), shot detail, ShotServer, an MCP tool, the AI advisor or the change-beans dialog — the system SHALL re-upload it with `?replace=1`, through the shared upload path, while the Decent switch and the shared "Auto-update shots" setting are on. A change made while the re-upload cannot be sent (upload off, needs sign-in, offline) SHALL be remembered on the shot and sent when uploading resumes. Writes the uploader itself makes to record upload state SHALL NOT trigger a re-upload.

#### Scenario: Rating added after upload
- **WHEN** the user rates a shot that was already uploaded
- **THEN** the shot is re-posted with `?replace=1`
- **AND** the account copy shows the new rating

#### Scenario: Edit while offline
- **WHEN** an uploaded shot is edited while the Decent server is unreachable
- **THEN** the shot stays marked replace-pending and is offered by Upload missing shots

#### Scenario: Recording upload state
- **WHEN** the uploader records a successful upload on a shot
- **THEN** that write does not cause another upload of the same shot

### Requirement: Missing shots are tracked and offered for upload per destination

The system SHALL NOT retry an upload automatically beyond its 3 attempts. An upload that still fails after them (no response, no connection, a server error) SHALL be recorded on the shot for that destination and cleared when the shot uploads; a permanent rejection is not a failure. Each destination's card on the Shot Upload tab, and on the ShotServer settings page, SHALL offer an Upload missing shots button while that destination is switched on and connected and is missing at least one eligible, non-rejected shot, showing how many and how many of them failed; with none missing it SHALL offer no button. Pressing it SHALL send edits the destination missed first (Decent: replace-pending; Visualizer: unsent edits), then those shots, newest first, through the shared upload path, at most 5 per batch with at least 30 seconds between batches, only while no espresso, steam, hot water or flush operation is in progress, resuming when the machine is idle again and after an app restart. While it runs the card SHALL show its progress in place of the button.

#### Scenario: Upload fails three times
- **WHEN** a new shot's Decent upload gets no response on all 3 attempts
- **THEN** the shot is recorded as failed for Decent, nothing retries it automatically, and the Decent card offers Upload missing shots with 1 failed

#### Scenario: Pressing the button
- **WHEN** a user with 1,000 shots the Decent account does not hold presses Upload missing shots while the machine is idle
- **THEN** shots are uploaded newest first, at most 5 per batch, with at least 30 seconds between batches, and the card shows the progress

#### Scenario: Shot started mid-run
- **WHEN** the user starts an espresso while missing shots are uploading
- **THEN** no new upload request is started until the machine is idle again
- **AND** the run resumes where it stopped

#### Scenario: Nothing missing
- **WHEN** every eligible shot is uploaded or rejected
- **THEN** the card offers no button until a shot is missing again

#### Scenario: Never automatic
- **WHEN** a destination is switched on, an account is linked or the app starts, and the button was not pressed
- **THEN** no saved shot is uploaded by the history mechanism

### Requirement: Retry and rejection rules

A 2xx response whose body is the API's `{"ok":true,...}` SHALL record the shot as uploaded, including a first upload answered `"duplicate":true`, which means the server already holds that id; a 2xx without `"ok":true` stored nothing and SHALL be transient. A replace answered `"duplicate":true` kept the server's earlier copy: the shot SHALL stay marked as having an edit to send, and the user SHALL be told the edit was not saved. A transport failure or timeout, or HTTP 404, 405, 408, 410, 429 or 5xx (an endpoint or server problem, not the shot), SHALL be transient: the request SHALL be retried up to 3 attempts, 2 s then 4 s apart, and after that the shot SHALL be recorded as failed for that destination and offered by Upload missing shots, never marked rejected. HTTP 401 SHALL put the account in the needs-sign-in state. HTTP 403 — the machine's serial is not registered to the account — SHALL drop what is queued for Decent and end any Upload missing shots run, and tell the user that machine serial is not in their Decent account. Any other 4xx SHALL record the shot as permanently rejected with its status. A rejected shot SHALL NOT be retried automatically unless its metadata changes afterwards.

#### Scenario: Server error
- **WHEN** an upload returns HTTP 503 three times
- **THEN** the shot is not marked rejected
- **AND** it is recorded as failed and offered by Upload missing shots

#### Scenario: Invalid document
- **WHEN** an upload returns HTTP 400
- **THEN** the shot is recorded as rejected with status 400
- **AND** a missing-shots run moves on to the next shot and never offers this one again

#### Scenario: Machine not in account
- **WHEN** an upload returns HTTP 403
- **THEN** shots queued for Decent are dropped, any Upload missing shots run ends, and the user sees that the machine's serial is not registered to their Decent account

#### Scenario: Already on the server
- **WHEN** the server responds `"stored":false,"duplicate":true`
- **THEN** the shot is recorded as uploaded and not sent again

### Requirement: Per-shot upload state is persistent

Each shot SHALL durably record: when it was last uploaded successfully, the server's shot id, the machine serial it was uploaded under, whether a replacement is pending, any permanent rejection with its HTTP status and time, and when its upload last failed. This state SHALL survive app restarts and SHALL be the only basis for deciding what Upload missing shots offers.

#### Scenario: Restart mid-run
- **WHEN** the app is restarted partway through an Upload missing shots run
- **THEN** shots already uploaded are not uploaded again, and the run resumes with the rest

### Requirement: Upload status and links are visible

The settings section SHALL show the account state and the result of the most recent upload attempt. A shot that has been uploaded SHALL show a read-only "Uploaded to Decent" status with a "View on decentespresso.com" link on its detail page, beside the existing Visualizer status, opening `https://decentespresso.com/support/espressomachine?view=chart&sn=<serial>&id=<server id>`. A rejected shot SHALL show that it was rejected.

#### Scenario: Uploaded shot detail
- **WHEN** the user opens the detail page of an uploaded shot
- **THEN** a "View on decentespresso.com" link opens that shot in the user's Decent account

### Requirement: One Upload button for every destination

The post-shot review page (which owns uploading; the shot detail page stays read-only) SHALL have a single Upload button, the one that was the Visualizer upload button. It SHALL send the reviewed shot — just pulled or opened from history — to every destination that is switched on and connected: Visualizer, Decent, or both. It SHALL be hidden when no destination is on and connected. For Decent it SHALL work whether or not automatic upload is on, SHALL apply the same eligibility rule as Visualizer (no cleaning or descaling records, nothing under the shared minimum length), SHALL also retry a shot recorded as rejected, SHALL re-send an already-uploaded shot with `?replace=1`, SHALL apply the machine-identity rule, and SHALL report success or the failure reason for each destination.

#### Scenario: Both destinations on
- **WHEN** Visualizer and Decent are both switched on and connected and the user taps Upload
- **THEN** the shot is sent to both, and each destination's result is shown

#### Scenario: Only Decent on
- **WHEN** Visualizer is switched off and Decent is on and connected
- **THEN** Upload sends the shot to Decent only

#### Scenario: Retry a rejected shot
- **WHEN** the user taps Upload on a shot Decent had rejected
- **THEN** it is sent to Decent again and, on success, its rejection is cleared

#### Scenario: Not eligible
- **WHEN** the user taps Upload with Decent on but no machine serial available
- **THEN** nothing is sent to Decent and the user is told why

### Requirement: Web and MCP settings parity

The ShotServer settings page SHALL let the user link and unlink the Decent account, switch Decent and Visualizer on and off, change the shared Upload settings, and see the account state. MCP `settings_get` / `settings_set` SHALL expose both destination switches (`visualizerEnabled`, `decentEnabled`), the shared Upload settings (`uploadAutomatically`, `updateAutomatically`, `uploadMinDurationSec`), and the read-only `decentAccountState`. On the web, Connect SHALL verify an account through the same code the app uses before saving it. Neither surface SHALL expose the encrypted password, a password or an account name.

#### Scenario: Switch Decent on from the web
- **WHEN** the user switches Decent on from the ShotServer settings page
- **THEN** the in-app switch reflects it

