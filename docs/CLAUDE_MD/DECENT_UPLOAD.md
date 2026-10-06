# Decent account shot upload

Uploads shots to the owner's account at decentespresso.com, as de1app (`plugins/shot_upload`) and
Decaid (`decentespresso/shot-upload`) do. The OpenSpec change is `add-decent-shot-upload`, rolled out in
three stages: manual upload (#1988), automatic upload (#1990), then Upload missing shots. API reference:
https://decentespresso.com/support/api/

## Pieces

| | |
|---|---|
| `DecentAccount` (`src/network/decentaccount.*`) | `login_test` exchange, sign-in state, authenticated redirect. Every call after linking authenticates through `applyAuth()`, and only while Linked. |
| `SettingsDecent` | `decent/email`, `decent/cryptpw` (never exported or imported), `decent/needsSignIn`, `decent/enabled` (the Decent switch; linking turns it on). `active` = switched on + linked + not waiting to sign in again. |
| `SettingsUpload` | When/what to upload, shared with Visualizer: `autoUpload`, `autoUpdate`, `minDuration`, stored under the pre-split `visualizer/*` keys. Each destination has only its switch and account. |
| `DecentShotRecord` | Saved shot → Decaid `ShotRecord` JSON. Field mapping follows de1app's `converter.tcl`. |
| `DecentShotUploader` | One attempt at a time; maps Decent's answers to the shared outcomes; records the upload itself. |
| `shots.decent_*` (migrations 42, 44) | Upload time, server id, the serial it was filed under, replace-pending (an edit Decent did not take), rejection, failure. |

**Auth.** The password goes to `login_test` once. The encrypted password it returns is stored and sent as HTTP Basic
`email:cryptpw` on every later call (`basicAuthHeader()` in `httpauth.h`, shared with Visualizer; the password is never
trimmed). Sign-in and uploads time out after 60 s (on 2026-10-04 the server took 25-29 s per upload and 33-38 s per page). Each sign-in outcome is logged with its duration, HTTP status and Qt error code. No OAuth: neither Decent app uses it, and it needs a client
registration.

**Serial.** Read from MMR `0x803830` on connect (`DE1Device::serialNumber`), cleared on disconnect. It is not captured when a
shot is saved: a first upload uses the connected machine's serial (Decaid's rule for legacy shots), and only the serial an
upload was filed under is kept (`decent_serial`), so a replacement lands on the same machine. With no machine connected, a
first upload is refused (`NoMachine`). The simulator reports `SIM-DE1`, which no account can own, so its uploads come back 403 and never reach an account. To test a real upload from the simulator, set your serial for this run over MCP: `settings_set {"simulatorSerialNumber": "<your serial>"}`. It is never saved; restarting the app restores `SIM-DE1`.

**Responses** (one table for both destinations, `ShotUploadDestination::responseOutcome`; D15):
- 2xx with `"ok":true`, including `"duplicate":true` on a first upload → uploaded.
- A 2xx without `"ok":true` (a captive portal, a proxy) is treated as transient.
- Transport error, timeout, 404, 405, 408, 410, 5xx → up to 3 attempts (retries after 2 s and 4 s, made by `ShotUploads`), then recorded as failed (`decent_failed_at`).
- 429 → recorded as failed after its one attempt, and every send to that destination waits 10 minutes
  (`SettingsUpload::noteRateLimited`, stored so a restart keeps it). For Visualizer, a 429 to the pull or bean repair
  starts the same wait.
- 401 → needs sign-in; credentials are not sent again until the account is signed in again.
- 403 → the serial is not in the account.
- Other 4xx → rejected and recorded (`decent_rejected_*`, written by `ShotUploads`); an edit clears it.
- A replace answered `"duplicate":true` → `NotReplaced`, and the shot is marked replace-pending: the server kept its earlier
  copy. As of 2026-10-04 decentespresso.com answers every `?replace=1` this way; reported to Decent.
- Cleaning/descaling records and shots under the shared minimum length are not sent (`uploadIneligibility()`).

**One path.** `ShotUploads` decides when for both destinations (D13 in the change's design): the shot saved after an extraction, any edit, and the Upload button, layout action and MCP `shots_upload`. `DecentShotUploader` is a `ShotUploadDestination`; it is handed one shot at a time and never decides on its own. `ShotUploads` makes every attempt and records failures and rejections the same way for both destinations (D15). An edit marks an uploaded shot replace-pending (`noteEdited`) even when nothing is sent, for Upload missing shots.

**Ordering.** The upload is prepared on the storage's serial DB worker (`runAfterQueuedWrites`), so it reads an edit the
review page saved a moment earlier.

Every request body is written to `Documents/last_decent_upload.json`, and the server's reply to `Documents/last_decent_upload_response.txt`.

**Golden file.** `tests/data/decent/accepted_shotrecord.json` is a body decentespresso.com accepted on 2026-10-04.
`payloadHasTheShapeTheServerAccepted` (`tst_decentshotupload`) fails when the serializer's shape drifts from it.
Re-capture it when Decent changes the contract.

## Upload missing shots

Nothing retries a failed upload on its own after its 3 attempts. Each destination card (app and ShotServer) offers
**Upload missing shots** while that destination lacks something (D14):

- **What:** `ShotUploads::findMissing`, one selection over each destination's `heldCondition`/`unsentEditCondition`.
  Edits it never received come first (`decent_replace_pending`, `visualizer_dirty` on a held shot), sent as updates
  so only their edited fields go; then shots it does not hold, newest first. Rejected shots and those `uploadIneligibility` excludes are never offered.
- **How:** 5 at a time through the destination's queue, batches at least 30 s apart, none while
  `MachineState::isOperating()`. Each send takes a turn from the destination's background pacer
  (`paceBackground`; Visualizer's is the 4 s pacer its pull and bean repair share, which keeps them all inside its
  200-requests-per-10-minutes limit). A shot that fails its attempts is recorded (`<dest>_failed_at`) and the run
  moves on; a sign-in or account refusal, from any send, ends it and clears that queue. Start, pause and end (with sent
  and failed counts) are logged at INFO under the destination's marker.
- **Restart:** the run's start is kept in `upload/missingRun/<dest>` (device-local, not exported). At startup, or
  when the destination comes back on, the run resumes, leaving out shots that failed since it started. A history
  that cannot be read starts nothing and keeps the run for later.
- **Counts:** `ShotUploads.missing`, recounted on the DB worker after any outcome, save, edit or account change.
  The web page reads `GET /api/settings/upload-missing` and starts a run with `POST /api/settings/{dest}/upload-missing`.

## Cleaning up test uploads

Re-uploading the same shot replaces it once Decent applies `?replace=1` (it does not as of 2026-10-04). Until then a wrong
document can only be trashed, which stays recoverable until a purge. Shot ids and serials come from the local database:

```bash
sqlite3 shots.db "SELECT id, decent_shot_id, decent_serial FROM shots WHERE decent_uploaded_at IS NOT NULL"
```

Get the encrypted password. Give curl only the email and it prompts for the password, which keeps it out of shell history:

```bash
curl -u 'you@example.com' https://decentespresso.com/support/api/login_test
```

List, trash and restore with the encrypted password:

```bash
curl -u 'you@example.com:CRYPTPW' 'https://decentespresso.com/support/api/shots?sn=SERIAL&limit=20&preview=0'
```

```bash
curl -X POST -u 'you@example.com:CRYPTPW' 'https://decentespresso.com/support/api/shot_trash?sn=SERIAL&action=trash&ids=ID1,ID2'
```

```bash
curl -X POST -u 'you@example.com:CRYPTPW' 'https://decentespresso.com/support/api/shot_trash?sn=SERIAL&action=restore&ids=ID1'
```

**`action=purge` deletes everything in the trash permanently, and Decent keeps no copy.** Do not use it for test cleanup.
