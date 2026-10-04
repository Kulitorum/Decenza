# Decent account shot upload

Uploads shots to the owner's account at decentespresso.com, as de1app (`plugins/shot_upload`) and
Decaid (`decentespresso/shot-upload`) do. The OpenSpec change is `add-decent-shot-upload`, rolled out in
three stages: manual upload, then automatic upload, then the history backlog. API reference:
https://decentespresso.com/support/api/

## Pieces

| | |
|---|---|
| `DecentAccount` (`src/network/decentaccount.*`) | `login_test` exchange, sign-in state, authenticated redirect. `applyAuth()` is the only place auth is set. |
| `SettingsDecent` | `decent/email`, `decent/cryptpw` (never exported or imported), `decent/needsSignIn`, `decent/enabled` (the Decent switch, default off). `active` = switched on + linked. |
| `SettingsUpload` | When/what to upload, shared with Visualizer: `autoUpload`, `autoUpdate`, `minDuration`, stored under the pre-split `visualizer/*` keys. Each destination has only its switch and account. |
| `DecentShotRecord` | Saved shot → Decaid `ShotRecord` JSON. Field mapping follows de1app's `converter.tcl`. |
| `DecentShotUploader` | One request at a time; retry classes; writes the result to the shot row. |
| `shots.decent_*` (migration 42) | Upload time, server id, the serial it was filed under, replace-pending, rejection. |

**Auth.** The password goes to `login_test` once. The encrypted password it returns is stored and sent as HTTP Basic
`email:cryptpw` on every later call. No OAuth: neither Decent app uses it, and it needs a client registration.

**Serial.** Read from MMR `0x803830` on connect (`DE1Device::serialNumber`). It is not stored per shot. A first upload uses
the connected machine's serial, which is Decaid's rule for legacy shots. A replacement reuses `decent_serial`, so it lands
on the same machine. With no machine connected, a first upload waits. The simulator reports `SIM-DE1`, which no account can own, so its uploads come back 403 and never reach an account. To test a real upload from the simulator, set your serial for this run over MCP: `settings_set {"simulatorSerialNumber": "<your serial>"}`. It is never saved; restarting the app restores `SIM-DE1`.

**Responses** (Decaid's classes):
- 2xx, including `"duplicate":true` → uploaded.
- Transport error, 408, 429, 5xx → retried 3 times (2 s, 4 s), then left untouched.
- 401 → needs sign-in.
- 403 → the serial is not in the account.
- Other 4xx → rejected and recorded.
- A replace answered `"duplicate":true` → `NotReplaced`: the server kept its earlier copy. As of 2026-10-04 decentespresso.com answers every `?replace=1` this way; reported to Decent.

**One button.** The review page's Upload button sends to every active destination (`visualizerActive`, `decent.active`). Stage 1: Decent ignores the automatic settings, so nothing reaches it except through that button.

**Ordering.** The upload is prepared on the storage's serial DB worker (`runAfterQueuedWrites`), so it reads an edit the
review page saved a moment earlier.

Every request body is written to `Documents/last_decent_upload.json`, and the server's reply to `Documents/last_decent_upload_response.txt`.

## Cleaning up test uploads

A wrong document is fixed by uploading the same shot again: the id is the shot's uuid, so it replaces. A shot that should not
be there is trashed, which stays recoverable until a purge. Shot ids and serials come from the local database:

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
