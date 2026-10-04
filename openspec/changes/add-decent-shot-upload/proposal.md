## Why

Decent now stores owners' shots in their Decent account at decentespresso.com, where they get shot history, charts, profile lineage and (later) machine-health and fleet comparisons. Both of Decent's own apps upload there — de1app through its `shot_upload` plugin and Decaid through `shot-upload.reaplugin` (source of truth: github.com/decentespresso/shot-upload). A Decenza owner's shots never reach it, so their account stays empty. Matching the two Decent apps closes that gap.

## What Changes

- **Link a Decent account.** A settings section where the user enters their decentespresso.com email and password once. Decenza calls `GET /support/api/login_test`, keeps only the encrypted password the server returns (`cryptpw`), and discards the plaintext. This is exactly what Decaid (`DecentAccountService.login`) and de1app do. Later calls authenticate with HTTP Basic `email:cryptpw`. Unlink clears both values. OAuth2/PKCE is out of scope: neither Decent app uses it, and it needs a client registration from Decent.
- **Auto-upload new shots.** Decent gets its own on/off switch, off by default as in Decaid. When it is on, each saved espresso shot that passes the shared minimum length is POSTed to `/support/api/shot_upload` as a Decaid `ShotRecord` JSON document carrying the machine serial.
- **Re-upload edited shots.** When an already-uploaded shot's metadata changes (beans, grinder, notes, rating, TDS…) and automatic update is on, Decenza re-posts it with `?replace=1`, so the account copy matches the local one.
- **History upload.** Unlike both Decent apps, existing history is uploaded only when the owner presses **Upload history** on a destination's card, once per destination; the button then disappears. The run goes in small throttled batches, only while the machine is idle. Transient failures retry; permanent 4xx rejections are recorded per shot and not retried automatically.
- **Per-shot upload state.** Each shot row records when it was uploaded, the server's shot id, the serial it was uploaded under, and any permanent rejection. This is what keeps backlog drain and replace idempotent across restarts.
- **View in account.** A "View my shots on decentespresso.com" action calls `GET /support/api/authenticated_redirect` and opens the one-time URL in the browser, already signed in. Each uploaded shot also gets a direct link (`/support/espressomachine?view=chart&sn=<sn>&id=<id>`) on the shot detail.
- **One Upload button.** The review page's Visualizer button becomes a single Upload button. It sends the shot to every destination that is switched on and connected, and also retries a shot Decent skipped or rejected.
- **Read the machine serial.** Decenza never reads the DE1's serial number today. The server needs it, because shots are filed under the serial and the account must own it. Decenza will read it on connect (MMR `0x803830`, as de1app's `get_sn` does), send it with each upload, and show it on the About tab's DE1 card. It is not stored in the shot itself; only the serial a shot was uploaded under is kept, as part of its upload state.
- **Reorganise the Visualizer settings tab into "Shot Upload".** A card per destination — Visualizer and Decent account — each with a large on/off switch and its account. Below them, ONE set of upload settings shared by both: upload automatically, update edits automatically, minimum length. These keep Visualizer's existing values. Edit After Shot and Clear Notes on Start, which control the post-shot review rather than uploading, move to Machine → App Behavior, keeping their stored values.
- **Surface parity.** The account link, both switches and the shared settings appear on the ShotServer web settings page and in MCP `settings_get`/`settings_set`. The encrypted password is never exposed by either.
- The wiki manual gets a short entry for the new setting.

Implementation is staged and gated on the live server: manual upload first, then automatic upload, then the history upload button (design D12, D14).

No breaking changes. Visualizer upload is untouched and independent; a user can upload to both.

## Capabilities

### New Capabilities
- `decent-account-link`: linking and unlinking a decentespresso.com account (login_test exchange, encrypted-credential storage, auth-failure state, authenticated browser redirect), reusable by later Decent API features.
- `decent-shot-upload`: uploading shots to the linked account — the payload contract, auto-upload of new shots, replace-on-edit, the one-time history upload, retry/rejection rules, per-shot upload state, and the manual and view actions.

### Modified Capabilities
- `data-transfer-coverage`: the Decent account credentials SHALL be excluded from backup and device migration (they are an account secret, like the Visualizer password). Per-shot upload state SHALL travel with the shot rows, so a migrated history does not re-upload.
- `settings-ui`: the Visualizer tab becomes the "Shot Upload" tab (a switch and account per destination, one shared set of upload settings); the tab-order and Auto-Update requirements are updated to match; Edit After Shot and Clear Notes on Start join the Machine tab's App Behavior column; the About tab's DE1 card shows the machine serial number.

## Impact

- **New C++**: a Decent account client and a Decent shot uploader under `src/network/`, plus a `ShotRecord` JSON serializer that shares the shot-loading path the Visualizer uploader already uses.
- **Settings**: new `SettingsDecent` and `SettingsUpload` domain sub-objects (all three registration edits each). The three shared settings move from `SettingsVisualizer` to `SettingsUpload` under unchanged keys, and Visualizer gains an on/off switch.
- **Database**: one migration adding the upload-state columns to `shots`.
- **MainController**: wiring for shot-saved, shot-metadata-updated and machine-phase signals into the uploader.
- **BLE**: one extra MMR read on connect (serial number) in `DE1Device`.
- **QML**: `SettingsVisualizerTab.qml` is rebuilt as the Shot Upload tab, with its label changed in `SettingsTabs.qml` and the settings search index updated. Two cards move to the Machine tab. The review page's upload button serves both destinations, and the detail page gets a Decent status card. Every string is translated.
- **ShotServer**: the settings page section; **MCP**: settings exposure (bump `McpSurfaceVersion` if the surface changes).
- **Logging**: a new registered subsystem for the Decent upload, per `docs/CLAUDE_MD/LOGGING.md`.
- **Network**: new outbound HTTPS to `decentespresso.com`, made only once the user links an account and enables upload.
- **Docs**: wiki manual entry; `docs/CLAUDE_MD/` gets a short reference doc for the integration.
