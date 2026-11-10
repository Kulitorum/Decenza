# ShotServer

ShotServer hosts the browser client and shared transport, plus MCP, home automation, Pocket and device-migration routes.

## TLS certificate files

Windows, Linux and Android encode PEM with OpenSSL memory BIOs and save through Qt
(`tlspemwriter.h`). No C runtime file handle crosses into the OpenSSL DLL; this
avoids Windows' `OPENSSL_Applink` dependency and supports Unicode file paths.

## Browser entry points

`/` opens the packaged Wasm app; `/wasm/` remains its compatible entry. The old HTML pages and their legacy-only upload, restore and management endpoints have been removed. Existing bookmarks to them no longer work. Authentication, MCP, documented home-automation routes, Pocket integration and device migration remain separate services.

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

Open `/#settings` (or `/wasm/#settings`) and select the relevant tab. Native and Wasm use the same settings pages and Qt Remote Objects services.

| User path | Shared implementation |
|---|---|
| History & Data: backup download/restore | `SettingsHistoryDataTab` → `HostFileDownload` → `BackupService`; native ZIP export and ZIP/older backup restore use the canonical host storage operations. |
| Screensaver: personal media | `SettingsScreensaverTab` → `ScreensaverService`; the requester selects files (several upload one per transfer), the host validates and stores them, and `list-personal`/`delete-personal` manage single items. |
| Debug: diagnostic log and AI conversation archive | `SystemLogDownload` → `SystemLogService`; `ConversationArchiveDialog` → `ConversationSession` (also opened by the chat overlay's Report button). Debug support downloads are available in production; developer controls remain gated. |
| Debug: shot database file and memory usage | `HostFileDownload` → `BackupService` `prepareDatabaseDownload` (a ZIP of a `VACUUM INTO` snapshot); `MemoryUsageDialog` → `SystemLogService` `memorySnapshot`. |
| Debug: Upload and install APK | `SettingsDebugTab` → `UpdateService`; shown only when the host supports local APK installation. |
| AI: Connect AI Apps (MCP) | `SettingsAITab`; setup/help links use the existing MCP installer/script/certificate endpoints. `/mcp` remains a protocol endpoint on the same listener. |

The browser uses the shared services above; file upload and restore do not use the removed HTML endpoints. Follow [SHARED_UI.md](SHARED_UI.md) when changing these workflows.

## Remote Web Access

The web interface through the Tailscale Funnel. `RemoteAccess` owns the public
listener: `GET /web/<token>` (`SettingsNetwork::remoteWebToken`, separate from the
MCP token) sets the `__Host-decenza_remote` admission cookie and redirects to `/`.
A later connection carrying a valid admission is handed to ShotServer **unread**
(`adoptRemoteWebConnection`, so the WebSocket upgrade still finds its handshake).
ShotServer re-checks every request on such a socket, both before the upgrade and
at header-complete: no valid admission closes it silently, and a path outside
`RemoteWebRoute::isAllowedPath` gets a `404`.

The allowlist (`src/network/remotewebroute.h`) is deny-by-default and checked on the raw path. It names only the current interface's routes,
so legacy HTML pages, assets, bookmark redirects and legacy-only APIs are never
served remotely. Add a route there when the web client starts calling one. The
symptom of a missing route is `Remote Web Access does not serve <path>` in the log.
Admissions persist under `webAuth/remoteWebSessions`, in the same store as
authenticator sessions. "Require authenticator code" still applies on top.

## File layout

- `shotserver.cpp` — server, authentication gate, routing, home automation and MCP dispatch.
- `shotserver_backup.cpp` — device-migration backup endpoints.
- `shotserver_session.cpp` and `remotesessionserver.cpp` — shared session and remote-object transport.
- `shotserver_layout.cpp` — library thumbnail endpoint used by Wasm.
- `shotserver_auth.cpp`, `shotserver_remoteweb.cpp`, `shotserver_screensaver.cpp`, and `shotserver_webclient.cpp` — authentication, remote access, media and client assets.

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

## Session QtRO upgrade

`GET /api/session/objects?packageId=…&hostId=…` upgrades on the existing listener.
The server validates the matching package and server instance, origin and authenticated
session. The transport bounds binary input/output, revalidates the cookie while active,
and closes on revocation. This is the UI transport; route and service implementation
belongs in [SHARED_UI.md](SHARED_UI.md).

## Shared native and Wasm UI

For page, control and UI-facing service changes, follow [SHARED_UI.md](SHARED_UI.md).
This document remains the source map and contract reference for ShotServer HTTP routes,
authentication and session upgrades.
