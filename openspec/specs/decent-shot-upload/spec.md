# decent-shot-upload Specification

## Purpose
Uploads the user's espresso shots to their linked Decent account, so they appear in the account's shot history alongside shots from de1app and Decaid. Covers the payload, new shots, edited shots, the history backlog, and the per-shot record of what was sent.

## Requirements

### Requirement: Decent switch and shared upload settings
Uploading to Decent SHALL have its own on/off switch, off until an account is linked; linking switches it on. Automatic behaviour SHALL follow the shared Upload settings ("Auto-upload shots", "Auto-update shots", "Minimum Duration") that also govern Visualizer, with no Decent copies. No shot SHALL be sent while the switch is off or no account is linked. Turning the switch on SHALL NOT send saved shots; Upload missing shots offers them.

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
Every uploaded document SHALL carry `machine.serialNumber`, and `machine.firmwareVersion` and `machine.model` when known. A first upload SHALL take these from the DE1 connected at upload time; the serial is not stored with the shot. A replacement SHALL reuse the serial the shot was first uploaded under. If no DE1 is connected when a first upload is due, the shot SHALL NOT be sent or marked rejected, and stays offered by Upload missing shots.

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

#### Scenario: DE1 that reports no serial, one DE1 in the account
- **WHEN** the connected DE1 reports serial 0 and the account's machine list has one DE1
- **THEN** shots are uploaded under that DE1's serial

#### Scenario: DE1 that reports no serial, several DE1s in the account
- **WHEN** the connected DE1 reports serial 0 and the account has several DE1s, none matching only by model
- **THEN** a dialog asks which one it is, once per app run or sign-in, and shots are uploaded under the one chosen

#### Scenario: Signed in before the machine list was read
- **WHEN** the connected DE1 reports serial 0 and the account has no machine list
- **THEN** a first upload is not sent, and the user is told to sign out and sign in again

### Requirement: Serial 0 resolves to an account DE1
A DE1 reporting serial 0 SHALL be filed under a DE1 from the account's machine list, read at sign-in from `/support/api/sn?onlyespressomachines=1&withskus=1` and kept until sign-out. The choice SHALL be, in order: the DE1 the user picked, the account's only DE1, or the only DE1 of the connected model.

#### Scenario: Account has one DE1

- **WHEN** a DE1 reports serial 0 and the account lists exactly one DE1
- **THEN** uploads are filed under that DE1's serial

### Requirement: Ambiguous serial 0 asks the user
When several DE1s remain and none is chosen, the user SHALL be asked in a dialog, at most once per app start or sign-in, and there SHALL be no permanent on-screen control for it. Until a serial is settled, a first upload SHALL NOT be sent or marked rejected, and the user SHALL be told to sign out of the Decent account and sign in again.

#### Scenario: Several DE1s and none chosen

- **WHEN** a DE1 reports serial 0 and the account lists several DE1s with none chosen
- **THEN** the user is asked once, and no upload is sent or marked rejected until a serial is settled

### Requirement: Simulator serial
In simulation mode the machine reports the fixed serial `SIM-DE1`, which no Decent account can own, so the server refuses its uploads with 403. MCP `settings_set` `simulatorSerialNumber` SHALL make the simulator report a given serial until restart. That value SHALL never be saved, SHALL have no on-screen control, and an empty value SHALL restore `SIM-DE1`.

#### Scenario: Simulator exercises the request path without an account

- **WHEN** an upload is sent while the simulator reports `SIM-DE1`
- **THEN** sign-in, the request and the response handling run, and the server's 403 is the expected outcome

### Requirement: Payload is a Decaid ShotRecord document
Each upload SHALL be a `POST` to `https://decentespresso.com/support/api/shot_upload` with `Content-Type: application/json`. The body SHALL be one Decaid `ShotRecord` JSON document, UTF-8 encoded exactly once, with top-level `id`, `timestamp`, `measurements`, `workflow`, `annotations`, `machine`, `app` and `schemaVersion` set to 1. Fields with no recorded value SHALL be omitted, never sent as empty strings or zeros.

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

#### Scenario: Identifiers and app metadata

- **WHEN** a shot is uploaded
- **THEN** `id` is the same on every upload of that shot and does not collide with shots from other devices or apps, `timestamp` is the shot start in ISO 8601 UTC with milliseconds, and `app` is `{"name":"decenza","version":<app version>,"sourceFormat":"decenza"}`

### Requirement: Measurement series layout
Each `measurements` entry SHALL carry `machine` (`timestamp`, `state`, `flow`, `pressure`, `targetFlow`, `targetPressure`, `mixTemperature`, `groupTemperature`, `targetMixTemperature`, `targetGroupTemperature`, `profileFrame`) and, when a scale was in use, `scale` (`timestamp`, `weight`, `weightFlow`). A sample with no recorded value for a channel SHALL carry `0`, keeping the parallel series aligned.

#### Scenario: Missing channel value in a sample

- **WHEN** a sample has no recorded `targetGroupTemperature`
- **THEN** that sample carries `0` for the channel, so the series stay aligned

### Requirement: Workflow context fields
`workflow` SHALL carry `name` (the profile title), `profile` (the profile run, in de1app v2 JSON) and `context` with `targetDoseWeight`, `targetYield`, `grinderModel`, `grinderSetting`, `coffeeName`, `coffeeRoaster`, `baristaName` and `finalBeverageType`, plus `extras` holding `roastDate` (ISO `yyyy-mm-dd`), `roastLevel` and `grinderRpm`.

#### Scenario: Unrecorded roast date is omitted

- **WHEN** a shot has no roast date recorded
- **THEN** `workflow.context.extras` omits `roastDate` rather than sending an empty string

### Requirement: Annotations
`annotations` SHALL carry `actualDoseWeight`, `actualYield`, `drinkTds`, `drinkEy`, `enjoyment` (0 to 100) and `espressoNotes`.

#### Scenario: Enjoyment is sent as a 0 to 100 value

- **WHEN** a shot is rated 80 for enjoyment
- **THEN** `annotations.enjoyment` is `80`

### Requirement: New shots upload once they are saved

With the Decent switch on and the shared "Auto-upload shots" setting on, the system SHALL upload each eligible shot after it has been saved to history, using the saved row as the source of truth, through the shared upload path (shot-uploads). Uploads SHALL be sent one at a time: a shot saved while another upload or a backlog batch is in flight SHALL be queued and sent next.

#### Scenario: Shot finishes
- **WHEN** an eligible espresso shot is saved
- **THEN** it is uploaded and its upload is recorded on the shot

#### Scenario: Two shots back to back while offline
- **WHEN** two shots are saved while the Decent server is unreachable
- **THEN** both are recorded as failed and offered by Upload missing shots, and neither is marked rejected

### Requirement: Edited shots are re-uploaded with replace
When the metadata of an uploaded shot changes, the system SHALL re-upload it with `?replace=1` through the shared upload path, while the Decent switch and the shared "Auto-update shots" setting are on. A change that cannot be sent (upload off, sign-in needed, offline) SHALL be remembered on the shot and sent when uploading resumes. Writes the uploader makes to record its own upload state SHALL NOT trigger a re-upload.

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

#### Scenario: Edit from any surface re-uploads

- **WHEN** a shot's metadata is changed from the post-shot review, shot detail, ShotServer, an MCP tool, the AI advisor or the change-beans dialog
- **THEN** it is re-uploaded with `?replace=1` if auto-update is on

### Requirement: Missing shots are tracked and offered for upload per destination
The system SHALL NOT retry an upload automatically beyond its 3 attempts. An upload still failing after them SHALL be recorded on the shot for that destination and cleared when the shot uploads. A permanent rejection is not a failure. A destination's card SHALL offer an Upload missing shots button only while the destination is switched on and connected and at least one eligible, non-rejected shot is missing; otherwise it SHALL offer none.

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

#### Scenario: Button shows missing and failed counts

- **WHEN** a destination is missing 4 shots, 2 of which failed
- **THEN** its Upload missing shots button shows both counts

### Requirement: Upload missing shots batches
Upload missing shots SHALL first send the destination's missed edits (replace-pending on Decent, unsent edits on Visualizer), then its shots newest first, through the shared upload path. It SHALL send at most 5 per batch, at least 30 seconds apart, and only while no espresso, steam, hot water or flush is in progress, resuming when idle and after restart. While running, the card SHALL show progress instead of the button.

#### Scenario: A run pauses while the machine is busy

- **WHEN** an Upload missing shots run is in progress and a shot starts
- **THEN** the run pauses and resumes once the machine is idle again

### Requirement: Retry and rejection rules
A transport failure, a timeout, or HTTP 404, 405, 408, 410, 429 or 5xx SHALL be transient, since it reflects the endpoint or server rather than the shot. Such a request SHALL be retried up to 3 attempts in total, and after that the shot SHALL be recorded as failed for that destination and offered by Upload missing shots, never marked rejected.

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

#### Scenario: Retry timing on transient failure

- **WHEN** a transient failure occurs on the first attempt
- **THEN** the retries wait 2 s, then 4 s, before the shot is recorded as failed

### Requirement: Upload success recording
A 2xx response whose body is `{"ok":true,...}` SHALL record the shot as uploaded, including a first upload answered `"duplicate":true`. A 2xx without `"ok":true` stored nothing and SHALL be transient. A replace answered `"duplicate":true` kept the server's earlier copy, so the shot SHALL stay marked as having an edit to send, and the user SHALL be told the edit was not saved.

#### Scenario: Duplicate first upload counts as uploaded

- **WHEN** a first upload is answered `{"ok":true,"duplicate":true}`
- **THEN** the shot is recorded as uploaded

### Requirement: Auth and serial errors
HTTP 401 SHALL put the account in the needs-sign-in state. HTTP 403, meaning the machine's serial is not registered to the account, SHALL drop what is queued for Decent, end any Upload missing shots run, and tell the user that the machine serial is not in their Decent account.

#### Scenario: Serial not registered

- **WHEN** an upload is refused with HTTP 403
- **THEN** queued Decent uploads are dropped and the user is told the machine serial is not in their account

### Requirement: Other 4xx rejects the shot
Any other 4xx SHALL record the shot as permanently rejected with its status. A rejected shot SHALL NOT be retried automatically unless its metadata changes afterwards.

#### Scenario: Rejected shot stays rejected

- **WHEN** a shot is rejected with HTTP 400 and nothing about it changes
- **THEN** it is not retried automatically

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
The post-shot review page SHALL have a single Upload button, the one that was the Visualizer upload button, which sends the reviewed shot to every destination that is switched on and connected. It SHALL be hidden when none is. For Decent it SHALL work whether or not automatic upload is on, SHALL apply the same eligibility rule as Visualizer, and SHALL report success or the failure reason for each destination. The shot detail page stays read-only.

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

#### Scenario: Shot detail page has no upload control

- **WHEN** a shot is opened on the shot detail page
- **THEN** no Upload button is offered there

### Requirement: Decent upload from review
The Upload button SHALL, for Decent, retry a shot recorded as rejected, re-send an already-uploaded shot with `?replace=1`, and apply the machine-identity rule.

#### Scenario: Review upload retries a rejected shot

- **WHEN** the user presses Upload on a reviewed shot recorded as rejected for Decent
- **THEN** the shot is sent again to Decent

### Requirement: Web and MCP settings parity
The ShotServer settings page SHALL let the user link and unlink the Decent account, switch Decent and Visualizer on or off, change the shared Upload settings, and see the account state. On the web, Connect SHALL verify an account through the same code the app uses before saving it. Neither surface SHALL expose the encrypted password, a password or an account name.

#### Scenario: Switch Decent on from the web
- **WHEN** the user switches Decent on from the ShotServer settings page
- **THEN** the in-app switch reflects it

### Requirement: MCP upload settings
MCP `settings_get` and `settings_set` SHALL expose `visualizerEnabled` and `decentEnabled`, the shared `uploadAutomatically`, `updateAutomatically` and `uploadMinDurationSec`, and the read-only `decentAccountState`.

#### Scenario: MCP reads account state

- **WHEN** an MCP client calls `settings_get`
- **THEN** the response includes `decentAccountState` and both destination switches
