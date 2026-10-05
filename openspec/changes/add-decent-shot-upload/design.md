## Context

See proposal.md for the motivation and specs/ for the behaviour. The reference implementations are Decaid's `shot-upload.reaplugin` (`plugin.js`, v0.2.3) with its `DecentAccountService` (`lib/src/services/account/decent_account_service.dart`), and de1app's `plugins/shot_upload` (`plugin.tcl`, `converter.tcl`). The server contract is https://decentespresso.com/support/api/.

Current Decenza state that shapes the approach:
- **Visualizer is the template, and also the warning.** `VisualizerUploader` has two parallel payload builders, `buildShotJson` (live, from `ShotDataModel`) and `buildHistoryShotJson` (static, from `ShotProjection`). They have already drifted: history omits `by_weight_raw` and `machine_state`. It has no durable retry queue, and re-upload-on-edit lives in QML (`PostShotReviewPage.maybeAutoUpdateVisualizer`), not C++.
- **`ShotHistoryStorage::shotMetadataUpdated(shotId, ok)` is the edit convergence point.** It is emitted by `requestUpdateShotMetadata`, the ShotServer edit path (`shotserver.cpp:1996`) and MCP `shots_update` (`mcptools_write.cpp:325`), and `ShotHistoryExporter` already relies on it. The Visualizer link writeback emits `visualizerInfoUpdated` instead, even though it bumps `updated_at`.
- **`shots.uuid TEXT UNIQUE NOT NULL`** is created at save time and is the dedup key in `importDatabaseStatic`, so it survives device migration.
- **The DE1 serial is never read.** `MMR::SERIAL_NUMBER = 0x803830` is defined but not requested; post-connect reads (`de1device.cpp:2603-2610`) cover model, firmware and GHC. No machine identity is stored per shot.
- **Secrets are plain `QSettings`** (Visualizer password, AI keys, MQTT password). They are protected by `SettingsSerializer::sensitiveKeys()` (excluded from backup and migration), web redaction (`redactedSecret`/`applySecretString`), and MCP exclusion. There is no keychain integration anywhere in the repo.
- Schema is at version 41. Migrations run synchronously before `m_ready`.

## Goals / Non-Goals

**Goals:**
- Byte-for-byte the same server-facing behaviour as Decaid: endpoint, auth, document shape, replace semantics, idle-only backlog, retry classes.
- One payload builder, sourced from the saved history row, for live, backlog, replace and manual uploads.
- Upload state in the database, so idempotency survives restarts and migration.

**Non-Goals:**
- OAuth2/PKCE. Neither Decent app uses it, and it needs a client registration. The account class keeps auth behind one method so a bearer token can be added later without touching the uploader.
- OS keychain storage for the encrypted password. Decaid uses `flutter_secure_storage`; Decenza has no keychain layer on any platform, and adding one for five platforms is its own change. The encrypted password is a server-issued credential, not the user's password, and gets every protection the Visualizer password has. Recorded as a follow-up.
- `steam_upload` (steam records for machine health) and `applog_upload`. Both are natural next uses of the linked account, and out of scope here.
- Reading shots back from the account (`shots`, `shot_get`, `curves`).
- Changing Visualizer upload behaviour beyond moving its controls into the new tab.

## Decisions

### D1. Auth: email + `cryptpw` over HTTP Basic, in a `DecentAccount` class
`DecentAccount` (`src/network/decentaccount.{h,cpp}`, `QML_ELEMENT` + `QML_UNCREATABLE`, reached as `MainController.decentAccount`) owns `link(email, password)`, `unlink()`, `state` (`NotLinked` / `Linked` / `NeedsSignIn`), `email`, `openAccountInBrowser()` (authenticated_redirect), `reportAuthFailure()`, and one `applyAuth(QNetworkRequest&)` that sets the Basic header. All Decent traffic goes through `applyAuth`, which is the single seam for a future bearer token. It uses the shared `QNetworkAccessManager`.
- `login_test` success test is the same as Decaid's: status 200, trimmed body non-empty and not `"0"`.
- Basic auth goes in the header only. The API also accepts URL parameters and cookies; neither is used, because a URL ends up in logs.
- *Alternative considered:* reuse `VisualizerUploader`'s request plumbing. Rejected, because its auth and URL constants are Visualizer-specific and it is already overloaded (bag sync, reconciliation, bean repair).

### D2. Settings: `SettingsDecent`, a shared `SettingsUpload`, and a switch per destination
- **`SettingsDecent`** keys: `decent/email`, `decent/cryptpw`, `decent/needsSignIn` (bool, persisted so a 401 is not retried every launch), and `decent/enabled` (the Decent switch, default off).
  - Only `email`, `enabled` and the derived account state are `Q_PROPERTY`s. `cryptpw` has a C++-only accessor, so QML cannot read it.
  - The email and `cryptpw` are never exported or imported, not even with `includeSensitive`; the web and MCP surfaces exclude them too.
- **`SettingsUpload`** (new) owns the settings both destinations share: `autoUpload`, `autoUpdate`, `minDuration`. They move off `SettingsVisualizer` but keep their keys (`visualizer/autoUpload`, `visualizer/autoUpdate`, `visualizer/minDuration`), so stored values carry over without a migration and the settings-store identity rule holds.
  - *Alternative considered:* leave them on `SettingsVisualizer` and have Decent read `Settings.visualizer.visualizerAutoUpload`. Rejected: a setting that governs both destinations must not be named after one of them.
- **`SettingsVisualizer`** gains `visualizer/enabled` (the Visualizer switch, default on), so existing users see no change. Every automatic Visualizer path gates on `visualizerActive` (switch + credentials) — after-shot upload, auto-update on close, MCP auto-update, bag sync — as does the review page's Upload button.
- Both new domains follow the three-edit checklist in `docs/CLAUDE_MD/SETTINGS.md`, including `QML_FOREIGN` in `settings_qml.h`.
- **MCP:** in Stage 1 the existing `visualizer*` names kept working against `SettingsUpload`. Stage 2 renames them to `uploadAutomatically`, `updateAutomatically`, `uploadMinDurationSec` and adds the switches (task 8.2, `McpSurfaceVersion` 1.12.0).

### D3. Serial number: read on connect, used at upload time
`DE1Device` adds `issueMMRReadWithRetry(MMR::SERIAL_NUMBER, …)` to the post-connect reads, and a `parseMMRResponse` branch that takes the 32-bit value (de1app's `Data0`). It exposes it as `Q_PROPERTY QString serialNumber`, empty when unread or 0. The simulator reports `SIM-DE1` (`DE1Device::kSimulatedSerial`): non-numeric, so no account owns it and the server refuses its uploads with 403. That lets the whole upload path run on the simulator without a simulated shot ever reaching an account. To test a successful upload, MCP `settings_set simulatorSerialNumber` swaps in a real serial for this app run only: an in-memory `DE1Device` field, never saved and with no UI, so it cannot outlive the test session or be switched on by accident. The About tab's DE1 card binds to it (selectable read-only `TextEdit`, accessible name "Serial number: <value>").

The serial is **not** stored with the shot. A first upload takes serial, model and firmware from the connected device when the request is built:
- model maps `MachineModel` to `"DE1"`, `"DE1+"`, `"DE1PRO"`, `"DE1XL"`, `"DE1CAFE"`
- firmware is `firmwareBuildNumber()` as a string

The serial used is written to `decent_serial` with the upload result. A replacement reuses `decent_serial`, so it lands on the same machine even if another DE1 is connected then (Decaid's `uploadedMachines` map). With no real device connected, a first upload is skipped for this pass, not rejected.
- *Alternative considered:* capture serial, model and firmware into new per-shot columns at save time, as Decaid does for new shots. That would file an offline shot under the right machine for a multi-machine owner who switches machines before it uploads. Rejected: rare enough to not earn three columns and a save-path change, and every shot saved before this change would still need the connected-machine rule anyway.

### D4. Schema migration 42
Adds to `shots`:
- `decent_uploaded_at INTEGER`, `decent_shot_id TEXT`, `decent_serial TEXT`
- `decent_replace_pending INTEGER NOT NULL DEFAULT 0`
- `decent_rejected_status INTEGER`, `decent_rejected_at INTEGER`

These are schema facts, so the version bump is gated on the columns landing. No index: the backlog query runs on a worker thread once per batch of five, every 30 s or more, and nobody waits on it. That is no user-felt cost, so per the complexity rule there is no index.

The columns are carried by `importDatabaseStatic` (spec: data-transfer-coverage); `importShotRecordStatic` imports shot files, which have no Decent state. They are **not** added to the positional `loadShotRecordStatic` SELECT. The uploader reads its own columns with its own query, and the shot-detail status uses a small dedicated read alongside the existing shot load. This avoids the three-struct positional cost noted at `shotserver.cpp:1888`.

### D5. One serializer, from the saved row
`DecentShotRecord::build(const ShotProjection&, const DecentMachineIdentity&) → QByteArray` (`src/network/decentshotrecord.{h,cpp}`) is static, pure and thread-safe.
- Live shots are uploaded **after `shotSaved`**, by loading the saved row, exactly like backlog and replace. So there is no `ShotDataModel` builder to drift.
- **Timeline:** the pressure series is the master. Each sample gets `timestamp = shotStart + x`, ISO 8601 UTC with ms. Every other series is resampled onto it with Visualizer's `interpolateGoalData`, moved out of file scope into a shared header (`src/network/shotpayloadhelpers.h`) so both uploaders call one copy. A missing value inside the series is 0 (spec).
- **Channels:**
  - `flow` = flow
  - `pressure` = pressure
  - `groupTemperature` = basket temperature
  - `mixTemperature` = mix temperature
  - `targetGroupTemperature` = temperature goal
  - `targetMixTemperature` = mix goal, else the temperature goal
  - `targetFlow` / `targetPressure` = goals
  - `profileFrame` = the frame number of the last phase marker at or before x
  - `state` = `{"state":"espresso","substate":"pouring"}`, as de1app's converter writes
  - `scale` is emitted only when the shot has a weight series: `weight` = cumulative weight, `weightFlow` = weight flow rate
- **Workflow:** `profile` is the shot's stored profile snapshot (already de1app v2), sent verbatim as `buildHistoryShotJson` sends it: re-serializing through `Profile` would claim values the shot never ran. A shot whose stored profile does not parse is not uploaded.
- **Context mapping:**
  - dose → `targetDoseWeight`, target weight → `targetYield`
  - resolved grinder brand+model → `grinderModel`; grinder setting → `grinderSetting`
  - bean type → `coffeeName`, bean brand → `coffeeRoaster`, barista → `baristaName`
  - beverage type → `finalBeverageType`
  - `extras`: `roastDate` (validated ISO date), `roastLevel`, and `grinderRpm` when set. `grinderModel` is `grinderDisplayName(brand, model)` from `shotpayloadhelpers.h`.
- **Annotations:** dose, final weight, TDS, EY, enjoyment, notes.
- **`id` = `shots.uuid`:** globally unique, stable across re-uploads, and preserved by migration's uuid dedup. The server's duplicate check then makes a re-upload after state loss harmless (`"duplicate":true` → mark uploaded).
- **`app`:** `{"name":"decenza","version":VERSION_STRING,"sourceFormat":"decenza"}`. **`schemaVersion`:** 1.
- `QJsonDocument::toJson(Compact)` emits UTF-8 once. This structurally avoids de1app's double-encoding bug, and a test asserts it.

### D6. `DecentShotUploader`: one in-flight request, event-driven
`src/network/decentshotuploader.{h,cpp}` owns a small state machine:
- **Inputs (Stage 2):** `onShotSaved(id)`, `onShotMetadataUpdated(id, ok)`, machine phase changes, settings and account-state changes, and app start.
- **Live and edits (Stage 2):** reached through `ShotUploads` (D13), which owns the queue for both destinations; the uploader sends what it is handed.
- **Edit (Stage 2):** `onShotMetadataUpdated(id, true)` calls a storage method that sets `decent_replace_pending = 1` **only if** `decent_uploaded_at IS NOT NULL`, then enqueues it.
  - Persisting the flag is what makes "edit while offline, upload disabled, or needs-sign-in" survive.
  - The uploader's own writeback goes through a separate storage method that emits `decentUploadStateUpdated`, never `shotMetadataUpdated`, so it cannot loop. The Visualizer link writeback also does not emit `shotMetadataUpdated`.
  - *Alternative considered:* compare `updated_at` against a recorded revision, as Decaid does. Rejected, because the Visualizer writeback bumps `updated_at` and would force a replace of every shot uploaded to both destinations.
- **Backlog (Stage 3):** a worker-thread query (`withTempDb`) selects the next ≤5 ids, newest first, where:
  - `decent_replace_pending = 1`, OR (`decent_uploaded_at IS NULL` AND `decent_rejected_status IS NULL`)
  - and the beverage type is not a maintenance type
  - and the duration is at least the minimum

  Replacements are ordered first. Each id is loaded and serialized on a worker thread, then posted back queued; the POST itself is async on the main thread through the shared QNAM. After a batch of 5, the next batch is scheduled ≥30 s later. That is a periodic rate limit, which the timer rule allows; it is not a guard.
- **Idle gate (Stage 3):** a boolean `m_machineBusy`, set from `MachineState::phaseChanged`. Busy = EspressoPreheating, Preinfusion, Pouring, Ending, Steaming, HotWater, Flushing, Refill, Descaling, Cleaning, Transport. Idle = Disconnected, Sleep, Idle, Heating, Ready. A transition to idle is a backlog trigger. Before each request the uploader checks the flag, and stops issuing when busy (event-based, per CLAUDE.md).
- **Retry:** transport error, 404, 405, 408, 410, 429, 5xx, or a 2xx without `"ok":true` → up to 3 attempts at 2 s then 4 s (Decaid's `RETRY_DELAY_MS * (i+1)`). After that the shot is left untouched for a later pass, and the drain pauses until the next trigger (phase→idle, new shot, settings change), which keeps it from spinning offline.
  - 401 → `DecentAccount::reportAuthFailure()`, which persists `needsSignIn` and stops everything.
  - 403 → in-memory `m_pausedNotRegistered` with the serial; a status message names it; cleared on re-link or restart.
  - Other 4xx → `decent_rejected_status` and `decent_rejected_at`; clear `decent_replace_pending`.
  - 2xx with `"ok":true` on a first upload (including `duplicate`) → write `decent_uploaded_at`, `decent_shot_id` (server `id`, falling back to uuid), `decent_serial`; clear pending and rejection.
  - A replace answered `duplicate` → the server kept its earlier copy: set `decent_replace_pending`, record nothing, report NotReplaced.
- **Unlink or disable:** clear the queue, abort any queued-not-sent work, ignore a late reply's scheduling (its state write still lands, since it is true).
- **MainController wiring:** constructs it with storage, account, settings domain, device and machine state, and connects the signals listed above.

### D7. Logging
New registered subsystem `DECENT` ("Decent") in `src/core/logtags.h` (two edits), used through `DIAG_*` per `docs/CLAUDE_MD/LOGGING.md`. Tiers by audience:
- `INFO`: link/unlink, successful uploads, not-sent outcomes (no machine, not linked, ineligible); in Stage 3 backlog start/finish and pause reasons.
- `WARN`: transient failure after retries exhausted, 403, 401.
- `DEBUG`: per-attempt detail.

Never log the Authorization header, the encrypted password, or request bodies. The resolution of a fault (e.g. "backlog resumed") is logged at the same tier as the fault.

### D8. Shot Upload tab layout
`SettingsVisualizerTab.qml` is rebuilt; its tab id stays `visualizer` so deep links and the search index keep working, and the label becomes "Shot Upload".
- **Layout:** two destination cards side by side (Visualizer, Decent account), each with a header switch, its account controls and its own actions. Below them is one Upload settings card for the shared settings (auto-upload, auto-update, minimum length). At narrow width all three stack in that order.
- **Shared components:**
  - `UploadDestinationCard.qml` provides the header (name + switch), status line and content slot.
  - `UploadAccountSection.qml` provides the sign-in fields, Connect / Disconnect and the error line, identical for both destinations.
  - Connect verifies before saving: `DecentAccount::link` via `login_test`, `VisualizerUploader::connectAccount` via an authenticated `GET /api/shots?items=1`. Both answer with the shared `AccountLink::Error` (None / Rejected / Unreachable), and a success switches that destination on.
  - This replaces Visualizer's save-as-you-type fields and separate Test Connection, which let unverified credentials into settings.
- **Moves:** Edit After Shot and Clear Notes on Start move to the Machine tab's App Behavior column, with the same `visualizer/*` keys. Renaming keys would need a settings migration for no user benefit.
- **Search index:** `qml/components/SettingsSearchIndex.js` entries are updated — new Decent entries, moved entries re-pointed to Machine.
- Uses `StyledTextField`, `Keyboard.commit()` before reading the password, `KeyboardAwareContainer`, full accessibility roles, and every string via `TranslationManager`.

### D9. Shot surfaces
- **ShotDetailPage:** a read-only "Uploaded to Decent" status card beside the Visualizer one. It shows a link opening `…/espressomachine?view=chart&sn=<decent_serial>&id=<decent_shot_id>`, or a rejected state.
- **PostShotReviewPage:** the existing Visualizer upload button becomes the one Upload button. One tap sends the shot to every destination that is switched on and connected:
  - Visualizer keeps its existing upload/PATCH logic.
  - Decent gets `DecentShotUploader::uploadNow(shotId)`, which bypasses automatic-upload and the rejected bar, and replaces if already uploaded.

  The button shows as in-sync only when every active destination already holds the current edit. Each destination's result is shown inline; Decent's wording comes from the shared `DecentUploadStatus.qml`. In Stage 1 the auto-update on close still reaches Visualizer only, and Decent joins it in Stage 2.

### D10. Web and MCP
- **ShotServer settings page:** mirrors the tab — a switch and account per destination (Decent: email + password → link server-side via `DecentAccount::link`, unlink, status), plus the shared Upload settings once. The password is never echoed, and `cryptpw` never leaves C++.
- **MCP `settings_get`/`settings_set` (Stage 2):** expose both switches (`visualizerEnabled`, `decentEnabled`), the shared settings under destination-neutral names (`uploadAutomatically`, `updateAutomatically`, `uploadMinDurationSec`), and read-only `decentAccountState`. No account names: MCP is reachable remotely, and the Visualizer username was already excluded. This changes the settings tool schema, so bump `McpSurfaceVersion`.
- No new MCP tool: the budget rule says a verb of an existing noun is an action, and nothing here needs one.

### D11. One implementation per shared behaviour
Where the two destinations do the same thing, there is one implementation, called for both:
- **"Is this destination active"** — switch on and account connected — is one property per destination on its settings domain (`SettingsVisualizer::visualizerActive`, `SettingsDecent::active`). MainController, the MCP tools (which hold `Settings`, not `MainController`) and QML all read it; none recomputes it.
- **Shot eligibility** (minimum length, not a maintenance beverage type) is one function, `uploadIneligibility()` in `shotpayloadhelpers.h`, used by Visualizer's `validateUpload` and MCP's pre-check (which each hand-rolled it) and by the Decent uploader's automatic path in Stage 2.
- **The settings card** grammar is one QML component (`UploadDestinationCard.qml`), and the shared settings appear once.
- **The review page** has one Upload button and one auto-update-on-close path, each dispatching to every active destination (auto-update reaches Decent from Stage 2).
- **Payload helpers** (`shotpayloadhelpers.h`: resampling, grinder name, debug-file location) are shared by both serializers.

### D12. Staged rollout
Implementation lands in three PRs, each gated on a check against the live server on Jeff's machine (see tasks.md):
1. **Manual upload, switches and shared settings.**
   - Account link, serial, payload, `uploadNow`.
   - The Shot Upload tab with a switch per destination and one Upload settings card.
   - The single Upload button.
   - Decent ignores the shared automatic settings until Stage 2.
2. **Automatic upload and replace-on-edit for Decent**, plus web and MCP parity.
3. **Backlog drain.**

Why this order: the payload is the only part that depends on a server we do not control, and a wrong document multiplied by a whole history is the expensive failure. Stage 1 proves the document on a handful of shots a person checked by eye before anything uploads unattended. Stage 3 is held until automatic upload has run in daily use.

Until Stage 3, a live upload that fails transiently is simply left un-uploaded; the Upload button retries it. Nothing in Stages 1-2 is thrown away by Stage 3: the drain reuses the same request core, response classes and state columns.

### D13. `ShotUploads`: one path for every destination (Stage 2)
`src/network/shotuploads.{h,cpp}` is the only place that decides when a shot is sent. It applies `SettingsUpload` once and queues the shot per active destination; each destination implements `ShotUploadDestination` (`isActive`, `busy`, `sendSavedShot(id, UploadOrUpdate|UpdateOnly)`, `noteEdited`). `VisualizerUploader` and `DecentShotUploader` are the two implementations.
- **Triggers:** `shotSaved` (MainController's save callback, when auto-upload is on); `ShotHistoryStorage::shotMetadataUpdated(id, true)` from any editor (when auto-update is on); `uploadNow` (Upload button, the "Upload last shot" layout action, MCP `shots_upload`). The review page holds its shot while open (`holdUpdates`/`expectHeldEdit`/`releaseUpdates`): only ITS saves, each announced before it is written, wait and go out once on close — the event-based replacement for its old pending flag. An edit from anywhere else goes out at once. Upload absorbs the page saves still being written, so closing after it sends nothing more.
- **Queue:** one shot at a time per destination, FIFO, a shot queued once (an UpdateOnly upgraded to UploadOrUpdate). A destination switched off or signed out has its queue dropped. Visualizer had no queue before: overlapping requests overwrote its per-upload state.
- **No duplicates:** each destination reads the saved row after queued writes. Visualizer PATCHes a shot with a `visualizer_id` and uploads otherwise; the id write after an upload is queued before the next read, so a second request patches. A PATCH answered 404 (deleted on visualizer.coffee) clears the dead link and, for an upload, sends the shot afresh. Decent replaces an uploaded shot; an edit landing while its upload is out keeps the shot replace-pending.
- **Results:** a Visualizer job ends with `savedShotFinished(shotId, error, skipReason)`, so the review page shows this shot's outcome whichever trigger sent it. A job decides it sent nothing from its own call, not from the shared `m_uploading`, which the migration-16 back-sync also sets.
- **MCP:** `shots_update` reports in `autoUpdateTo` only the active destinations that hold the shot (`ShotUploadDestination::holdsShot`). `shots_upload` refuses an ineligible shot only when no destination holds it.
- **Visualizer payload parity:** the live builder (`buildShotJson`) is gone; every Visualizer upload uses `buildHistoryShotJson`. To keep a new shot's upload as it was: the raw weight-flow series is stored in the sample blob and sent as `by_weight_raw`; `machine_state` (firmware, state, substate, headless) is read from the device at upload time, as before; `meta.time` is the last sample's time. Left different: `date` is the shot's time, not the upload moment seconds later, and `clock` can be a second off. The review page's overrides (edit fields laid over the shot) are gone: the row after the save holds the same values. The page no longer sends grinder strings, which the backend already ignored.
- **Edits reach Visualizer from every editor now.** Before, only the review page and MCP PATCHed it; ShotServer, the AI advisor and the change-beans dialog did not. One trigger for both destinations makes that uniform.
- **Web:** Connect on the ShotServer page calls `VisualizerUploader::connectAccount` / `DecentAccount::link`, the app's verified connect, replacing the page's own Visualizer test request and its unverified credential save. Every `link()` ends with `linkFinished`, `Cancelled` when Disconnect interrupts it, so a waiting web request is always answered.

## Risks / Trade-offs

- **[Serial MMR read returns 0 on old firmware or some boards]** → no serial means no upload (spec). The About tab shows "unknown". Verify on Jeff's DE1 before relying on it, and log the raw read at INFO once per connect.
- **[Backlog filed under the currently connected machine]** → every first upload uses the connected DE1's serial (Decaid's legacy-shot rule). An owner who replaced their DE1 gets the old machine's history filed under the new serial when it drains; the server rejects serials not in the account (403). Accepted — the same outcome Decaid gives every shot saved before it captured machine identity.
- **[Large backlog]** → 5 shots per ≥30 s is ~600/hour while idle. A 3,000-shot history drains in an afternoon of idle time. This is Decaid's cadence: it also sends 5 per batch and continues after 30 s while a batch fills.
- **[`cryptpw` in plain QSettings]** → same exposure as the Visualizer password today. Excluded from backup, migration, web and MCP. Keychain is a tracked follow-up.
- **[Edit paths that bypass `shotMetadataUpdated`]** → `coffeebagstorage.cpp` writes to `shots` in four places. Audit them during implementation; any that changes uploaded fields must also mark replace-pending.
- **[A bad upload lands in the user's real account]** → recoverable. A content mistake is corrected by re-sending the same `id` with `?replace=1`. A shot that should not be there is trashed by id with `shot_trash` (recoverable until `purge`). Only a wrong serial is beyond replace, because it files the shot under another machine; Stage 1 verifies the serial before the first upload, and uploads one shot and inspects it before any more. Each request body is saved to `last_decent_upload.json` for inspection.
- **[Server shape drift]** → the server is Decent's and evolves. Golden-file test of the serializer against a document the live server accepted, re-captured when the contract changes.
- **[Two destinations confuse users]** → the shared column component and identical section order make them parallel and visually separable. The manual entry explains each in one sentence.

## Migration Plan

- Migration 42 runs once at startup (adds the columns, NULL or 0). Existing shots start "never uploaded".
- Nothing uploads until the user links an account **and** turns upload on, so shipping is inert for existing users.
- Rollback: an older build ignores the new columns and keys; re-upgrading resumes from the recorded state.
