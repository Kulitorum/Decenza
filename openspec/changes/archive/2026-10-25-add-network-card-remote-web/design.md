# Design

## Context

See proposal.md for why. The current state that shapes the approach:

- `McpRemoteAccess` (`src/mcp/mcpremoteaccess.*`) owns a dedicated remote listener and the embedded tsnet node (`McpTunnelTsnet`). Funnel terminates TLS at the Tailscale edge and proxies plain TCP to `127.0.0.1:<port>`. The listener serves only `/mcp/<token>`, hands matching requests to `McpServer` in-process, and silently drops strangers behind the tunnel.
- The Wasm client (`src/webclient/remotesessionbackend.cpp`) takes `window.location.origin` as its only API origin. It loads assets from `/`, opens `/api/session/ws`, and refuses any other origin. So a remote interface has to be served from the Funnel host's root; a path prefix the client ignores will not work.
- ShotServer owns web auth: TOTP and persisted, hashed session cookies in `shotserver_auth.cpp`. Its request loop takes a `QTcpSocket*`, and `tryUpgradeSessionWebSocket()` hands an upgrade to `QWebSocketServer` on that same socket.
- Tailscale state reaches QML through `AISettingsService`. URL material (`connectorUrl`, `loginUrl`) is returned only by explicit requests and is never put in the broadcast snapshot (`aisettingsservicesource.cpp:145`).
- The Network card is shared QML, so it also renders in the browser. Its data and actions go through a Remote Objects service, per `SHARED_UI.md`.

## Goals / Non-Goals

**Goals:**
- One Tailscale identity, one Funnel host and one remote listener, serving two independently switched, independently tokenized routes.
- The web interface works unchanged over the public host, keeping its same-origin and package checks.
- The remote web route can reach strictly less than the LAN surface, defined in one place.

**Non-Goals:**
- Remote Web Access through Custom-URL mode, or any reverse proxy other than the embedded Funnel.
- An MCP tool that turns Remote Web Access on. An AI client must not be able to publish the interface. The switch itself is an ordinary network preference, so a browser of the web interface can toggle it exactly as it can toggle remote MCP today.
- Tailnet-only (non-Funnel) serving.
- Renaming the stored `shotServerEnabled` key or its MCP/settings-serializer field.

## Decisions

### One coordinator for both routes, renamed `RemoteAccess`
`McpRemoteAccess` becomes `RemoteAccess`, moved to `src/network/remoteaccess.*`. It talks to ShotServer through a small `RemoteWebHost` interface (`remotewebhost.h`), so it stays testable without linking ShotServer, which no test target compiles. It keeps the listener, tunnel, reachability probe, failed-token limiter and refusal logic, and gains a second route. Enable Network (`shotServerEnabled`) is the master switch for both routes, as it already was for local MCP. It runs the node when `shotServerEnabled && ((remoteMcpEnabled && mode == tailscale) || remoteWebEnabled)`. Remote MCP does not consult `mcpEnabled`, which opens only the local, LAN-wide endpoint, with each route accepted only while its own condition holds.

*Alternative:* a second listener with a second tsnet node. Rejected: that means two tailnet devices, two logins and two Funnel hostnames for one machine. The name changes because the old one would describe half of what the class does.

### Admission: safe URL → host-only cookie → root
`GET /web/<token>` (constant-time token compare) mints an admission session and responds `303 /` with `Set-Cookie: __Host-decenza_remote=<id>; Secure; HttpOnly; Path=/; SameSite=Lax`. Only the session id's hash is stored, through the same session store ShotServer already uses for TOTP sessions: one implementation with a separate namespace, not a second copy. After that, every request on the public host needs a valid admission cookie. Without one it gets the silent drop, including at `/`.

- The `__Host-` prefix makes the browser enforce host-only, Secure and `Path=/`.
- `Lax` rather than `Strict` so opening the root from a bookmark or another app still sends the cookie. Cross-site writes stay blocked by `WebRequestGuard` (Origin / Sec-Fetch-Site), which already treats `*.ts.net` as ours.

*Alternative:* carrying the token in every request path. Rejected: it breaks the client's root-origin assumption and leaks the token into history and the Referer header.

### Web requests are handed to ShotServer in-process
The coordinator decides a connection's route by peeking at its first request line, without consuming it. A web-route connection is then adopted by ShotServer through `ShotServer::adoptRemoteWebConnection(QTcpSocket*)`, which flags the socket as remote-web for its lifetime. ShotServer's normal loop then handles keep-alive, the WebSocket upgrade and auth. On a flagged socket, each request additionally:
1. needs a valid admission cookie, and is otherwise closed with no bytes;
2. needs a path that passes `isRemoteWebPath()`, and otherwise gets `404`;
3. is treated as already secure, so there is no HTTP→HTTPS redirect, because Funnel terminated TLS;
4. is never treated as local or trusted because its peer is `127.0.0.1`.

*Alternative:* piping bytes to ShotServer's own HTTPS port. Rejected: it means TLS to ourselves, Host/Origin rewriting, and a second socket per client.

Web-route connections are accepted only from the tunnel, meaning a loopback peer while the node is running. When Custom-URL MCP has made the listener bind `Any`, a LAN peer sending `/web/…` is refused.

### Allowlist, defined once
`isRemoteWebPath()` is deny-by-default:
- **Allowed:** the web entry and its packaged assets (`/`, `/wasm/…`), `/auth/*` and `/api/auth/*`, `/api/session/*` including `/api/session/ws`, and the `/api/…` routes the web client actually calls. Those come from an inventory of the client's request sites (task 3.2), not from a guess.
- **Denied explicitly**, and asserted in a test: `/api/backup/*` (device migration), `/legacy/*`, `/mcp*`.

**No legacy interface, by construction.** The allowlist is built from the current web client's request sites, so the legacy pages, their assets and their bookmark redirects are excluded because nothing in that list names them, not because a deny entry catches them. The predicate runs before routing, which means the old-bookmark redirects into `/legacy/` never fire remotely. An API used by both interfaces is allowed. An API that only legacy pages call is not, and the inventory in task 4.3 records which is which.

### TOTP gating reuses ShotServer's existing check
When `webSecurityEnabled` is on, adopted requests go through the same `checkSession` path as LAN requests, so the browser sees the normal authenticator sign-in after admission. When it is off, the admission cookie alone is sufficient. No new setting.

### Settings
`SettingsNetwork` gains `remoteWebEnabled` (default false) and `remoteWebToken`, which is lazy, 128-bit and base64url. `SettingsMcp::generateToken()` becomes a shared helper both domains call. Rotation replaces the token, clears all admission sessions and closes every adopted socket, leaving MCP untouched. The settings serializer and backup treat `remoteWebToken` exactly as they treat `remoteMcpToken`, matched rather than decided anew.

### Services
`ShotServerSettingsService` is the existing network-domain service, so it is extended rather than adding a sibling:
- **Snapshot:** `remoteWebEnabled`, `remoteAccessStatus` and `statusDetail`, `tunnelAvailable`, `tailscaleSetupNeeded`.
- **Private requests:** `requestRemoteWebUrls()` returns `{safeUrl, loginUrl}`; `rotateRemoteWebToken()`; `forgetTailscale(requestId)`.

`forgetTailscale` and `loginUrl` leave `AISettingsService`. The AI tab keeps `remoteMcpStatus`, the connector URL request and MCP rotation. The URL-material rule is unchanged: never in the snapshot.

### Reachability probe covers whichever route is on
The probe fetches `/mcp/<token>` when remote MCP is on. Otherwise it sends `HEAD /web/<token>`, which the route answers without minting a session. Any HTTP response proves the public path either way.

### QML
The Network card is declared **inline** in `SettingsConnectionsTab.qml`, not in a component file. `scripts/settings_search_index.py` indexes each control of a card declared on a tab, but treats a card component's controls as covered by the card. A separate file would have stopped "Require authenticator code" and "Remote Web Access" being search results of their own. No new QML file, so neither CMake source set nor the Wasm inventory changes.

The left column becomes a scrolling column: Network at its natural height, then Machine, which gets at least the height its content needs (`SettingsCard.contentImplicitHeight`) and fills any room left. A first version capped Network at 55% of the column instead, and Machine's connection log drew over itself on a tablet once Tailscale was active.

The Tailscale sign-in block, the Funnel setup dialog and the sign-out dialog **move** out of `SettingsAITab.qml`; they are not copied. The AI tab keeps a link that opens the Connections tab (`AppShell.settingsRequested("connections")`). The server and authenticator rows and the TOTP setup and reset dialogs move out of `SettingsHistoryDataTab.qml`.

Translation keys: the label "Enable Network" gets a new key. Moved strings whose wording still fits keep their keys, so existing translations survive. Strings that mention AI connectors get new keys with reworded text.

### Logging
The listener and the Tailscale node serve both routes, so their lines move from `[MCP]` to `[Network][RemoteAccess]` and `[Network][Tunnel]`. The marker descriptions in `logtags.h` say so. MCP request handling still logs as `[MCP]`.

## Risks / Trade-offs

- **The public safe URL is full machine control when the authenticator is off.** → Warn next to the QR code. Rotation is one tap, and the route stays off by default.
- **A loopback peer is easy to mistake for a trusted local client.** → Rule 4 above, plus a test that a flagged socket from `127.0.0.1` gets no local exemption.
- **Allowlist drift:** a new web-client API returns `404` over Tailscale only. → One predicate, a test that asserts the denials, and a manual check of the main pages over the public host.
- **Card height:** the Network card with a QR code shortens the Machine card on small tablets. → Check native at tablet resolution and in the browser at phone width. Inside the card, the QR code shows only while the route is active and URL-ready.
- **Large first load over Funnel:** the client bundle is tens of MB. → Accepted; it is the same bundle as the LAN, and browser caching applies. Note it in the manual if it is slow in practice.
- **Ordering:** this builds on the open Wasm change. → Implement against its current `main` state; this change archives after it.

## Migration Plan

No data migration. `shotServerEnabled` keeps its key and value. New settings default off. Rollback means turning the switch off, which leaves the token and identity intact.

## Open Questions

- Admission session lifetime: reuse `SESSION_LIFETIME_DAYS` unless testing on phones shows re-scanning is too frequent. This does not affect specs or tasks.
