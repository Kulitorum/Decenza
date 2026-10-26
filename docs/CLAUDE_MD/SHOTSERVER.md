# ShotServer Web UI

The ShotServer is the in-app HTTP server that exposes shot history, settings, layout editor, and AI endpoints to the browser. It is split across multiple files; this doc captures the conventions that the routing layer and the embedded JavaScript must follow.

## Browser entry points

`/` opens the packaged Wasm app; `/wasm/` remains its compatible entry. The
retained HTML interface lives at `/legacy/`, with its page links beneath that
prefix. `/index.html` redirects to the HTML landing page, and old non-root HTML
bookmarks remain compatible aliases. API, authentication, MCP, upload and download
endpoints keep their existing root paths. The two interfaces still use the same
host data and authorization; moving the HTML routes does not retire them.

The uncached entry stamps `/wasm/<manifest-sha256>/` as its asset base URL. The
manifest includes the actual asset hashes, so rebuilding the same source with
different toolchains or optimization settings changes the URL when bytes change.
Allowlisted code there is privately cacheable for a year. Entry documents,
errors and unversioned asset URLs remain
`no-store`. Downloads still pass manifest/hash verification, and the session
handshake still checks the package and current host epoch before enabling writes.

The browser header's fullscreen icon controls only that browser, using the
Fullscreen and Screen Orientation APIs where supported. iPhone Safari without
those APIs uses Add to Home Screen / Open as Web App instead. The entry declares
the home-screen icons (`/apple-touch-icon.png` for iOS, `/android-icon-192.png` for
Android, both from `ShotServerWebRoute::iconResource`) and keeps controls inside CSS
safe-area insets; the icons are public like the favicon, including before login.

## Shared support workflows

Open `/#settings` (or `/wasm/#settings`) and select the relevant tab. Native
and Wasm use the same settings pages and Qt Remote Objects services; they do
not open the retained HTML pages to implement these actions.

| User path | Shared implementation |
|---|---|
| History & Data: backup download/restore | `SettingsHistoryDataTab` → `BackupService`; native ZIP export and ZIP/older backup restore use the canonical host storage operations. |
| Screensaver: personal media | `SettingsScreensaverTab` → `ScreensaverService`; the requester selects files, while the host validates and stores them. |
| Debug: diagnostic log and AI conversation archive | `SystemLogDownload` → `SystemLogService`; `ConversationArchiveDialog` → `ConversationSession`. Debug support downloads are available in production; developer controls remain gated. |
| Debug: Upload and install APK | `SettingsDebugTab` → `UpdateService`; shown only when the host supports local APK installation. |
| AI: Connect AI Apps (MCP) | `SettingsAITab`; setup/help links use the existing MCP installer/script/certificate endpoints. `/mcp` remains a protocol endpoint on the same listener. |

The retained local-network HTML counterparts remain under `/legacy/` (including
`/legacy/upload`, `/legacy/upload/media`, `/legacy/restore` and
`/legacy/ai-conversations`). Existing API and file paths stay rooted at their
original URLs. Remote Web Access has the narrower allowlist described below;
retaining local HTML does not expose it through the public Funnel. Follow
[SHARED_UI.md](SHARED_UI.md) when changing these shared workflows.

## Remote Web Access

The web interface through the Tailscale Funnel. `RemoteAccess` owns the public
listener: `GET /web/<token>` (`SettingsNetwork::remoteWebToken`, separate from the
MCP token) sets the `__Host-decenza_remote` admission cookie and redirects to `/`.
A later connection carrying a valid admission is handed to ShotServer **unread**
(`adoptRemoteWebConnection`, so the WebSocket upgrade still finds its handshake).
ShotServer re-checks every request on such a socket, both before the upgrade and
at header-complete: no valid admission closes it silently, and a path outside
`RemoteWebRoute::isAllowedPath` gets a `404`.

The allowlist (`src/network/remotewebroute.h`) is deny-by-default and checked on the
raw path before `/legacy/` resolution. It names only the current interface's routes,
so legacy HTML pages, assets, bookmark redirects and legacy-only APIs are never
served remotely. Add a route there when the web client starts calling one. The
symptom of a missing route is `Remote Web Access does not serve <path>` in the log.
Admissions persist under `webAuth/remoteWebSessions`, in the same store as
authenticator sessions. "Require authenticator code" still applies on top.

## File layout

- `shotserver.cpp` — core server + route dispatch
- `shotserver_layout.cpp` — layout editor web UI (inline HTML/JS)
- `shotserver_shots.cpp` — shot history endpoints and the `/shots`, `/shot/<id>` and `/compare/` pages. `/shot/<id>` is the web counterpart of the app's shot page (`PostShotReviewPage`): it edits in place through `POST /api/shot/<id>/metadata`, refreshes its results from `GET /api/shot/<id>/outcome`, and uploads through `POST /api/shot/<id>/upload`. Its graph and comparison text come from `webtemplates/shot_graph.h` and `webtemplates/comparison_text.h`, shared with `/compare/`.
- `shotserver_backup.cpp` — backup/restore endpoints
- `shotserver_settings.cpp` — settings endpoints
- `shotserver_ai.cpp` — AI assistant endpoints
- `shotserver_auth.cpp` — authentication
- `shotserver_theme.cpp` — theme endpoints
- `shotserver_upload.cpp` — file upload handling
- `shotserver_recipes.cpp` — recipes REST + `/recipes` page (add-recipes)
- `shotserver_bags.cpp` — coffee-bag REST + `/beans` page (add-recipes), plus Bean Base search / AI extraction / bean image endpoints
- `shotserver_equipment.cpp` — equipment-package REST + `/equipment` page (add-recipes)
- `shotserver_session.cpp` — state/telemetry snapshots, bounded graph capture jobs, and read-only session upgrade validation
- `remotesessionserver.cpp` — WebSocket ownership, ordered snapshots, session expiry and buffer limits
- `remoteshotgraphsnapshot.cpp` — pure bounded serialization of captured native shot graph data
- `webtemplates/management_{css,html,js}.h` — shared style/shell/JS for the three management pages (see below)

The layout editor web UI is served as inline HTML/JS from `shotserver_layout.cpp`.

## Retained HTML helpers and shared app UI

The `/legacy/` HTML pages use `webtemplates/management_{css,html,js}.h` for shared
styles, shell markup and JavaScript helpers. Keep HTML-only changes within these
helpers; they do not define the native/Wasm UI implementation.

The legacy HTML grind/RPM fields use `<input>` plus `<datalist>` through
`webtemplates/grind_datalist_js.h` and `GET /api/grind-candidates`. That recorded
legacy behavior applies only under `/legacy/` and never licenses a different Wasm
control. Follow [SHARED_UI.md](SHARED_UI.md) for native and Wasm pages, controls and
UI-facing services.

## Async community endpoints (signal-based)

- Community API calls (`browse`, `download`, `upload`, `delete`) are async — they connect to `LibrarySharing` signals and wait for a callback. Use the established pattern: `QPointer<QTcpSocket>` + `std::shared_ptr<bool>` fired guard + `QTimer` timeout + `PendingLibraryRequest` tracking.
- **Always verify the operation was accepted** after calling `LibrarySharing` methods. Methods like `browseCommunity()` and `downloadEntry()` silently return if already busy (`m_browsing`/`m_downloading`). Check `isBrowsing()`/`isDownloading()` after the call and send an immediate error response if rejected — otherwise the request hangs until the 60s timeout.
- **Always log timeout and cleanup events.** Use `qWarning()` when a timeout fires, `qDebug()` when a response is dropped (socket disconnected) or when a duplicate callback is blocked by the fired guard.
- **Only one request per type** is allowed at a time (`hasInFlightLibraryRequest`), because `LibrarySharing` is a singleton that emits one signal consumed by whichever handler is connected.

## Cross-site requests

`WebRequestGuard::crossSiteReason()` (`src/network/webrequestguard.cpp`) runs in `onReadyRead()`
as soon as a request's headers are complete, before any body is buffered, and refuses with 403:
a `Host` that is not this machine (DNS rebinding: only IP literals, localhost, single labels,
exactly the machine's hostname and LAN/mDNS/Tailscale suffixes pass); an `Origin` naming
another host; and on `/api` or `/mcp` paths any `Sec-Fetch-Site` other than `same-origin` or
`none`. The `Origin` check is the defence for writes, and browsers send it over plain HTTP too,
so **every route with a side effect takes POST**; the `Sec-Fetch-*` headers reach only https,
the tsnet funnel and localhost (Fetch Standard, "append the Fetch metadata headers"), so on the
default `http://<ip>` page a GET from an `<img>` on another site is indistinguishable from curl.
A top-level navigation to a page from another site still opens; HTML responses carry
`frame-ancestors 'self'`, so no other site can frame a page and click through it. Non-browser
clients (curl, Home Assistant, MCP clients) send none of these headers and pass. No ShotServer
response carries `Access-Control-Allow-Origin` (`/mcp` keeps its own CORS handling behind its
access token, `mcpserver.cpp`), so a page on another site cannot read the server's JSON. The web
pages only ever fetch their own origin. `tests/tst_webrequestguard.cpp` holds the cases.

## JavaScript `fetch()` calls

- **Every `fetch()` must have a `.catch()` handler.** Never leave a fetch chain without error handling — silent failures leave the UI in a broken state (spinner stuck, editor blank, no feedback).
- **Check `r.ok` before `r.json()`** in fetch chains. Non-2xx responses with non-JSON bodies will throw on `.json()` and produce a misleading "Network error" instead of "Server error (500)".
- **Use `AbortController` with a timeout** for community API calls (client-side 45s, server-side 60s safety net).
- **Don't mutate state before async success.** For example, increment a page counter only after the fetch succeeds, not before — otherwise failed requests skip pages permanently.

## Recipes / bags / equipment surfaces (add-recipes)

- Endpoints: `GET/POST /api/recipes`, `POST /api/recipes/from-shot/<shotId>`, `GET/POST /api/recipe/<id>` plus `/clone`, `/archive` (body: `restore`/`delete`), `/activate`; `GET/POST /api/bags`, `GET/POST /api/bag/<id>` plus `/finish`, `/delete`, `/activate`, `GET /api/bag/<id>/image`; `GET /api/beans/search?q=…`, `POST /api/beans/extract` (async AI "get info from page"); `GET/POST /api/equipment`, `GET/POST /api/equipment/<id>` plus `/remove`, `/delete`, `/activate`. All behind the auth gate.
- **Full app parity (polish-shotserver-inventory-pages):** the bag create/update payloads accept the full app field set — `kind` (coffee/tea, create-only), the yield anchor (`yieldG`/`yieldRatio`), `rpmPinned`/`rpm`, `equipmentId`, freeze-lifecycle dates (`openedDate`/`storageHint`), and the descriptive attributes carried in the `beanBaseData` JSON blob (the page parses/merges it client-side); `beanBaseId` + `beanBaseData` together set a Bean Base canonical link (the update route propagates it to the bag's shots, like the in-app edit dialog). Equipment payloads accept the puck-prep flags as `puckPrep_<key>` booleans (`PuckPrep::flagKeys`). `/api/beans/search` and `/api/beans/extract` reuse `BeanBaseClient` + `AIManager` (the same backends as the MCP `bean_search` / `bag_extract_details` tools) via the async socket pattern (fired guard + timeout + always-respond).
- **Bag photos follow the product URL.** `GET /api/bag/<id>/image` keys the cache the way `BagCard.qml` does — the canonical id when the bag is linked, else `bag-<rowid>` for a manual bag that has a `link` in its blob (canonical-only meant a manually entered bag showed a placeholder on the web forever while the app card showed its photo). The create route warms the photo from the `bagCreated` handler, where the row id first exists. The update route accepts a **`refreshImage`** boolean: the page sets it when the user edited the URL to a new non-empty value, and the route then calls `BeanBaseClient::refreshBagImage()` — from the *success* handler, since the cache key is shared by every bag on the same canonical bean and must not be evicted for a write that failed. The reply echoes `imageRefreshed` so a caller can tell a refresh it asked for from one it did not get. The client owns the changed/unchanged decision because only the form knows the URL it opened with. MCP `bag_update` closes the same loop from its own side: it already pre-reads the current blob, so it compares the old and new `link` there and re-resolves with no race at all.
- **Stage-2 photos.** `POST /api/beans/extract` can return an `imageUrl` — the product photo the model read off a JS-rendered page that has no `og:image` to scrape, so it is the only photo such a bag will ever get. The editor holds it and sends it as **`extractedImageUrl`** on the next save (create or update), riding the existing round trip rather than adding one; the server derives the cache key, so the client never names a file. It wins over re-scraping the page, via `BeanBaseClient::replaceBagImageFromUrl()` — the replacing sibling of `cacheBagImageFromUrl()`, which lets a cache hit win and is therefore right only for warming a bag that has no photo yet.
- `POST /api/recipe/<id>/activate` calls `MainController::activateRecipe` — the single activation path shared with the idle pills and MCP; respond only on the matching `recipeActivated(id, success)`.
- Lifecycle guards are storage-enforced and surface as HTTP 409 (delete refused for rows with shots/references).

## Opt-in equipment-name precondition

`GET /api/equipment` advertises `capabilities.equipmentNamePrecondition: true`.
Clients may send `POST /api/equipment/<id>` with `{"name":"New label","expectedName":"Original label"}`.
Both values must be strings, and the precondition is supported only for a name-only update.
Storage compares the original raw name, validates uniqueness, and writes within one SQLite
write transaction. A changed name returns HTTP 409 with `code: "conflict"`; a duplicate
inventory name also returns 409 with its existing error message. Success retains the
`{updated:true,packageId:...}` contract. Clients without `expectedName` keep existing behavior.

HTTP clients can use the full `equipmentEditableSnapshotPrecondition` capability:
`expectedEditable` contains the original seven editable strings, including canonical puck
prep. Storage compares them in the same transaction as the update. Create, full edit,
activation, removal and deletion report request-specific completion. Creating with
`applyToActiveBag: true` applies the new equipment through the native settings service
before acknowledging success. These HTTP routes and authentication remain available to existing callers.

The retained JSON session stream and HTTP inventory response carry `equipmentRevision`.
Package writes and active-equipment changes advance it. Inventory reads report the revision
captured before the asynchronous read so older rows cannot be labelled fresh.
## Retained recipe-name precondition

`GET /api/recipes` advertises `capabilities.recipeNamePrecondition`. A client can send
exactly `{name, expectedName}` to `POST /api/recipe/<id>` for a name-only update. Storage
compares the raw original name inside its write transaction and preserves all unrelated
fields. A mismatch returns 409 `nameConflict` with `currentName`; a duplicate active name
returns 409 `nameInUse`. Callers without the precondition retain their existing contract.
The API remains available to existing callers.

## Retained JSON session API

`GET /api/session` returns a version-1 snapshot with sequence zero. An authenticated,
same-origin `GET /api/session/ws` upgrade on the same listener streams complete state and
telemetry snapshots every 250 ms, starting at sequence one for each connection. The envelope
contains `type: "snapshot"`, `protocolVersion`, a `hostId` UUID regenerated when the server
starts, `sequence`, `state`, `telemetry`, `machineUi`, and `capabilities: {subscriptionReadOnly: true}`.
The state and telemetry objects exclude integration keys. `machineUi` is the read-only UI
projection; action requests are documented separately below.

The upgrade requires an explicit matching Origin, passes `WebRequestGuard`, and uses the
existing cookie session when web security is enabled. Session validity is rechecked before
each snapshot. Incoming application messages close the connection: this channel accepts no
commands. Native snapshots are capped at 32 KiB and queued output at 64 KiB per client; the
existing connection limit includes upgraded sockets. Browser clients must invalidate stale
readings on disconnect, incompatible versions, changed host identity, or a sequence gap and
obtain a fresh preflight before resuming.

The JSON session API remains supported for existing clients. Wasm UI implementation
follows [SHARED_UI.md](SHARED_UI.md).

### Shared machine UI state

`machineUi` carries machine phase, simulator and connection state, DE1 state/substate codes,
temperatures, water levels, scale weight/flow, operation flags, steam auto-flush countdown,
heater/temperature-override state, active profile targets/ratio and brew/water readout
preferences. `steamPitchers` contains preset maps, selection and an ordered-preset revision;
missing settings are represented as unavailable. These fields are read-only snapshots.

`POST /api/session/steam/select-pitcher` adds integer `index` and
`steamPitchersRevision` to the session-action identity fields. The host checks the
current preset revision and index, then calls `MainController::selectSteamPitcher`
with host-owned captured milk. Concurrent preset edits/reordering return 409; no
operation starts from selection alone. `sessionActions.selectPitcher` advertises the
service. The historical page-specific HTTP routes below remain supported.

Existing HTTP clients can request page preparation, pitcher selection and five-second adjustments
through `/api/session/steam/prepare`, `/select-page-pitcher` and `/adjust-time`.
They add `controlContextId` to the identity fields; the first two also require
`index` and `steamPitchersRevision`, while adjustment requires `delta` of -5 or 5.
`machineUi.controlContextId` changes synchronously with raw DE1 state, substate or
connectivity, so a command for an earlier operation is rejected before any writes.
Preparation also checks that the selected pitcher still matches. The native
controller applies each compound user action using its current settings; clients
do not calculate a new timeout from an old snapshot. None starts steam flow.
These HTTP actions are advertised by `sessionActions.steamPageControls`.

`/api/session/steam/stop` and `/back` use the same context guard. Stop also requires
the boolean `expectedSoftStopped`; the host rejects an obsolete first-tap state
instead of treating it as Purge. `machineUi.steamSoftStopped` and the hardware two-tap preference identify the
current Stop/Purge state. The host resets it on raw Steam exit or disconnect and dispatches Purge
at most once per cycle.
Successful stop/back responses include a boolean `dismissPage` and confirm dispatch,
not hardware completion.

### Scale actions

`POST /api/session/scale/tare` and `/scan` require exactly `packageId`, `hostId` and a
canonical UUID `requestId`, with a 4 KiB body limit and no query parameters. They use the
existing origin/auth policy and dispatch through native `MachineState::tareScale()` or
`BLEManager::scanForDevices()`. The correlated `actionResult.accepted` confirms dispatch,
not hardware completion. The host rejects duplicates within a bounded 256-ID window; clients should not
automatically retry an uncertain result. `subscriptionReadOnly`
describes the WebSocket feed; HTTP actions are advertised separately in `sessionActions`.

### PORTAL status and reconnect

Session `machineUi` includes PORTAL state, active/reading flags, raw EC value,
saved-device ownership and machine-busy flag. An unattached PORTAL service projects as
disconnected. `POST /api/session/portal/reconnect` uses the same
identity, origin/authentication, 4 KiB request limit and bounded duplicate-ID window as
other session actions; `capabilities.sessionActions.portalReconnect` advertises whether
the host has the service. The host rejects unowned devices, active/disconnecting connections,
machine-busy state and active shot extraction. `accepted` confirms scan/reconnect
dispatch, not BLE connection success. The existing HTML/API behavior is unchanged.

### Retained latest-shot snapshot API

`GET /api/session/last-shot` returns one atomic saved-shot projection with package ID,
host ID and history revision (or `shot: null` for empty history). The native history
read runs off the main thread. Shared limits cap lists at 6,000 entries,
phases at 512 and the JSON response at 2 MiB; excessive data returns 413, and a changed
revision returns 409. This HTTP route remains available to existing callers.

### Retained layout snapshot API

`GET /api/session/layout` wraps the native cached layout object with package ID, host ID
and layout revision. Both input and serialized response are bounded to 256 KiB. It is
read-only, uses the existing request/authentication guard, and leaves `/api/layout` and
its HTML editor unchanged. Layout changes advance the session's `layoutRevision` as well
as the existing SSE notification.

### Retained graph snapshot API

When a native shot model is available, session snapshots also advertise
`capabilities.graphSnapshot: true` and a safe-integer `graphRevision`. The read-only
`GET /api/session/graph` endpoint returns the latest complete graph with its `hostId` and
`revision`, nine trace arrays, four goal arrays, phase markers, elapsed time, and target/final
weights. Points are `[x,y]`; a whole `null` entry separates goal segments. Limits are 6,000
entries per curve, 512 markers and 2 MiB of JSON. Invalid or excessive data returns an error
instead of a silently shortened graph.

The handler captures native values on the main thread and serializes them on one background
worker. Requests already waiting share a capture; at most eight additional requests wait
for the next capture, with excess requests returning 503. Replies are dropped for disconnected
sockets or an obsolete server instance. Model resets, tare and settling corrections advance
the graph revision. The HTTP endpoint
remains available to existing callers. Remote/mobile latency, host impact and multi-client
soak acceptance remain pending.

### Retained HTTP recipe shot-plan projection

HTTP recipe list/detail reads retain optional `shotPlan` facts (`available`, `baseTempC`, `yieldG`, `stepTempsC`, `temperatureText`) from the earlier Mac experiment. This is separate from the existing HTML `profileBaseTempC` field, whose resolution is unchanged. Exact installed titles take precedence over a recipe's frozen JSON. Catalog refresh caches normalized profile numbers and frame temperatures; request handlers capture that snapshot and the temperature unit before worker-thread serialization. More than 20 frames or invalid facts produce an unavailable plan rather than a truncated one.

### Preparation actions

`POST /api/session/preparation/{espresso,steam,hotwater,flush,milk-start,milk-cancel,milk-lock,milk-unlock}`
remains available for existing API callers and uses the identity-only request, 4 KiB cap, exact-route validation and bounded
request-ID deduplication as scale actions. Mode selection never starts a machine
operation. The host rejects changes that conflict with an active operation. Page
navigation does not cancel the host workflow.


### Session QtRO upgrade

`GET /api/session/objects?packageId=…&hostId=…` upgrades on the existing listener.
The server validates the matching package and server instance, origin and authenticated
session. The transport bounds binary input/output, revalidates the cookie while active,
and closes on revocation. This is the UI transport; route and service implementation
belongs in [SHARED_UI.md](SHARED_UI.md).

## Shared native and Wasm UI

For page, control and UI-facing service changes, follow [SHARED_UI.md](SHARED_UI.md).
This document remains the source map and contract reference for ShotServer HTTP routes,
authentication and session upgrades.
