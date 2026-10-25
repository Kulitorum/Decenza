# Tasks

## 1. Network card: Enable Network and authenticator

- [x] 1.1 Declare the Network card (a `SettingsCard`, searchId `networkCard`) inline in `SettingsConnectionsTab.qml`, so every control on it stays a settings-search result. Verify: the build is green, including the QML gate, and `tools/webclient/check_qml_inventory.py` passes with no new file.
- [x] 1.2 Move the server switch, the LAN address row, "Require authenticator code", Set Up Authenticator / Reset Security, and the TOTP setup and reset dialogs from `SettingsHistoryDataTab.qml` into the card. Label the switch "Enable Network" under a new key; keep `shotServerEnabled` and the existing authenticator keys. Verify: the toggle still starts and stops the server, and TOTP setup and reset still complete natively and in the browser.
- [ ] 1.3 Make the left column of `SettingsConnectionsTab.qml` a scrolling column holding the Network card (natural height) and then Machine, which always gets at least its content height (`SettingsCard.contentImplicitHeight`). Verify: on native at tablet resolution, with Tailscale active and the Remote Web Access QR showing, Machine is not squeezed or overdrawn, and in the browser at phone width nothing is clipped.
- [x] 1.4 Rename the History & Data right column to "Data", keeping "Your Data" counts and Factory Reset, and move the server keywords to the Network card. Verify: the settings search index regenerates (commit `SettingsSearchEntries.js`), and searching "server" or "Enable Network" opens the Network card.
- [ ] 1.5 Fix accessibility issues in the touched QML per `ACCESSIBILITY.md` (role, name, focusable, press action on every moved control). Verify: TalkBack/VoiceOver focus order in the card goes top to bottom.
- [ ] 1.6 Update the wiki manual's Connections and History & Data pages: where the network switch and authenticator now live, and the new name. Keep it to a few sentences.

## 2. Tailscale node controls move to the Network card

- [x] 2.1 Extend `ShotServerSettingsService` (native source and policy; the generic `executePrivate` needs no `.rep` change). Add `tunnelAvailable`, `tailscaleSetupNeeded`, `remoteAccessStatus` and `statusDetail`, and readiness flags (not the URLs) to the snapshot. Add private requests `requestRemoteWebUrls` (`safeUrl`, `loginUrl`), `rotateRemoteWebToken` and `forgetTailscale`. Remove `forgetTailscale` and `loginUrl` from `AISettingsService`. Verify: `tst_shotserversettingspolicy` accepts the three operations only with no arguments, and `tst_aisettingsservicesource` still shows no URL in the AI snapshot. The ShotServer snapshot is built only by ShotServer, which no test compiles, so check it by reading `refreshSnapshot()`.
- [x] 2.2 Move the Tailscale sign-in link and QR, the Funnel setup dialog, the sign-out row and its confirmation dialog, and the node status from `SettingsAITab.qml` into the Network card. Move them; do not copy. Show them while either consumer wants the node. Reword strings that mention AI connectors under new keys. Verify: with remote MCP in Tailscale mode and no identity, the login QR appears in the Network card and sign-out works from there.
- [x] 2.3 On the AI tab, keep the remote MCP switch, the mode, the custom URL, the connector URL with its QR, and rotation. In Tailscale mode, add a status line and a pointer to Settings → Connections → Network. Verify: the AI tab has no Tailscale login, setup or sign-out control, and the pointer opens the Connections tab.
- [ ] 2.4 Update the wiki manual's AI / remote-MCP section to say that Tailscale sign-in and setup are in the Network card.

## 3. Shared remote-access coordinator and settings

- [x] 3.1 Rename `McpRemoteAccess` to `RemoteAccess`, moved to `src/network/remoteaccess.*`, and its test to `tst_remoteaccess.cpp`. The test is renamed, not added as a new file. Update `docs/CLAUDE_MD/MCP_SERVER.md` and the log subsystem registration per `LOGGING.md`. Verify: the suite is green and `check_log_markers.py` passes.
- [x] 3.2 Add `remoteWebEnabled` and `remoteWebToken` to `SettingsNetwork`, and extract the token generator into one shared helper used by both MCP and web. Make the serializer and backup handle `remoteWebToken` the way they handle `remoteMcpToken`. Verify: a settings test shows the token is lazily created, stable across reads, and replaced on rotate.
- [x] 3.3 Implement the node-wanted predicate and per-route acceptance: the node runs if either route needs it, and disabling one route keeps the other working. Verify: tests in `tst_remoteaccess` cover web-only, MCP-only, both, and disabling MCP while web stays on (`/mcp/<token>` refused, node still running).
- [x] 3.4 Make the reachability probe use `/mcp/<token>` when MCP is on and `HEAD /web/<token>` otherwise. Verify: a test shows that web-only probing mints no admission session.

## 4. Remote web route

- [x] 4.1 Implement `GET /web/<token>` admission: a constant-time compare, an admission session hashed in ShotServer's existing session store under its own namespace, a `__Host-decenza_remote` cookie (Secure, HttpOnly, Path=/, SameSite=Lax), and `303 /`. A wrong or missing token gets the silent drop and counts against the failed-token limiter. Verify: tests cover a valid token, a wrong token, a missing token, and each token used on the other route.
- [ ] 4.2 Add `ShotServer::adoptRemoteWebConnection()` and the per-request rules for adopted sockets: a valid admission is required or the socket closes with no bytes, checked before the WebSocket upgrade and again at header-complete; no HTTPS redirect; no loopback trust. Verify: `tst_remoteaccess` shows the coordinator hands over an admitted connection with its request still unread, and refuses a forged cookie. ShotServer's own gate is checked live in 6.2, because no test target compiles ShotServer; it has no loopback exemption to test.
- [x] 4.3 Inventory every request site in the web client (`src/webclient`, shared QML services), marking each API as used by the current interface or by legacy pages only. Implement `isRemoteWebPath()` from the current-interface list, deny-by-default, and check it before routing so the legacy bookmark redirects never run remotely. Verify: a test asserts that `/api/backup/*`, `/legacy/*`, `/mcp*`, an old bookmark path that the LAN redirects into `/legacy/`, a legacy-only asset and a legacy-only API each return `404` with no redirect to an admitted socket, and that `/`, `/api/session/ws` and an inventoried API succeed.
- [ ] 4.4 Route adopted requests through the existing `checkSession` when `webSecurityEnabled` is on. That is ShotServer's normal auth middleware, unchanged. Verify live in 6.2: with the authenticator on, the phone gets the sign-in page; with it off, the interface opens directly.
- [x] 4.5 Implement web token rotation: a new token, cleared admission sessions, closed adopted sockets, and MCP untouched. Verify: a test shows the old safe URL and the old cookie are refused after rotation, while an MCP request with the MCP token still succeeds.
- [x] 4.6 Accept the web route only from the tunnel (a loopback peer while the node runs). Verify: a test shows a non-loopback peer sending `/web/<token>` is refused while Custom-URL MCP binds `Any`.
- [x] 4.7 Document adopted sockets and the allowlist in `docs/CLAUDE_MD/SHOTSERVER.md`.

## 5. Remote Web Access controls

- [x] 5.1 Add the "Remote Web Access" switch to the Network card, writing `remoteWebEnabled`. Disable it when the network is off (show the reason) or the build has no tsnet ("Not included in this build"). Never say "Wasm" in user-visible text. Verify: on a non-tsnet build the switch is disabled with that text, and turning Enable Network off greys it out with the reason.
- [ ] 5.2 Show the safe URL with Copy, a QR code (the existing QR component) and the authenticator-off warning, only while the route is reachable; otherwise show the status. Fetch the URL through `requestRemoteWebUrls()`. Verify: the URL appears only after Funnel answers, and the warning appears exactly when the authenticator is off.
- [ ] 5.3 Add web token rotation with a confirmation dialog through `rotateRemoteWebToken()`. Verify: after confirming, the card shows the new URL and QR, and the old URL no longer opens on a phone.
- [ ] 5.4 Add Remote Web Access to the settings search and the wiki manual: what it is, where it is, and that with the authenticator off the link alone gives control. Keep it to 3-5 sentences.

## 6. Integration checks

- [x] 6.1 Run the full suite through Qt Creator (`run_tests`, scope all). Verify: green, the QML gate passed, and `text-invariants` checks pass locally (log markers, settings search, test-source duplication).
- [ ] 6.2 Live check on a tsnet build: enable Remote Web Access, scan the QR code with a phone on cellular, sign in with the authenticator, and confirm the interface loads, live data updates and a write lands. Then turn the authenticator off and confirm direct entry. Record the result briefly, with no screenshot archive.
- [ ] 6.3 Live check of coexistence: remote MCP and Remote Web Access on at once, the claude.ai connector works, rotating one leaves the other working, and Sign out stops both.

## 7. AI tab: AI Advisor vs outside AI apps

- [x] 7.1 Split the AI tab into "AI Advisor Config" and "Connect AI Apps (MCP)" cards, one above the other, with intros saying what each uses and who pays; relabel the key "API Key (for AI Advisor)". Verify: search index regenerated, and the search snapshot retargets "Enable MCP Server" to `ai/aiApps`.
- [x] 7.2 Order the MCP card: switch, access, confirmation, Remote Access (recommended) with Claude and ChatGPT steps and copy-and-open buttons, then Discuss Shot with its needs-Remote-Access note. Say why Remote Access is greyed while MCP is off. Verify: translation-key conflict check passes.
- [x] 7.3 Move local MCP (setup link, network warning, status, setup guide) into a dialog behind an "Advanced…" link that settings search still finds. Verify: the index has "Local network MCP (advanced)" on `ai/aiApps`.
- [x] 7.4 Make remote MCP require Enable Network, and show it: the needs-network link under Enable MCP Server, the reason on the greyed Remote Access switch, and Tailscale named in the Remote Access description. Verify: `tst_remoteaccess` shows the connector URL disappears while the network is off and returns after, and that neither route nor the node is wanted with the network off.
- [ ] 7.5 Check in the running app: both cards and the wrapping provider buttons at tablet and phone width; the ChatGPT button opens the settings page; the Advanced dialog works. Note: the ChatGPT deep link (`#settings/Connectors`) is unverified and may land on ChatGPT's home instead of its settings page.

## Workflow follow-up

- Land after, or on top of, `replace-web-interface-with-wasm`.
- Archive with `openspec archive add-network-card-remote-web --yes` as the PR's final commit before merge.
