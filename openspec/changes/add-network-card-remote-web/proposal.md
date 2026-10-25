# Proposal

## Why

Network settings are split across unrelated tabs. The server switch and authenticator sit in History & Data, and the Tailscale node is configured inside the AI tab's MCP section, even though it is shared infrastructure. The web interface also works only on the LAN, so a phone away from home cannot open it. The Tailscale Funnel that already serves remote MCP is the obvious way to reach it.

## What Changes

- Add a **Network** card to Settings → Connections, at the top of the left column above Machine.
- Move the server switch into it and rename it from "Enable Server" to **"Enable Network"**. The stored `shotServerEnabled` key does not change. Its LAN address link moves with it.
- Move "Require authenticator code" and its Set Up Authenticator / Reset Security actions into the Network card.
- Leave the shot and profile counts ("Your Data") and Factory Reset on the History & Data tab.
- Move the **Tailscale node** controls from the AI tab to the Network card: sign-in link and QR, one-time Funnel setup, node status and Sign out.
- Keep AI-specific remote controls on the AI tab: remote MCP on/off, reachability mode (Custom URL / Tailscale), custom base URL, the MCP connector URL and QR, and MCP token rotation. When Tailscale is the chosen mode, the AI tab points to the Network card for the node controls.
- Add **"Remote Web Access"**, a separate switch in the Network card that is independent of remote MCP. When it is on, the web interface is published through the same Funnel host behind its own capability token: `https://<node>.<tailnet>.ts.net/web/<token>`. The card shows that URL with copy, a QR code for phones, and a rotate action.
- The remote web interface honours "Require authenticator code". With it on, the safe URL leads to the authenticator sign-in. With it off, the safe URL alone grants access, and the card says so next to the QR code.
- The remote web route reaches only the current web interface and the APIs it uses. The legacy HTML interface is never served remotely: no pages, assets, bookmark redirects or legacy-only APIs. It stays LAN-only. Device-migration endpoints and `/mcp` are not reachable either.
- The Tailscale node runs whenever either remote MCP in Tailscale mode or Remote Web Access needs it. It stops when neither does.
- **Enable Network becomes the master switch for remote MCP too.** Local MCP already needs it; the remote connector stops while it is off. The AI tab says what each part needs (Enable Network; Remote Access additionally Tailscale) and links to Connections → Network.
- Reorganize the AI tab into two cards so the in-app AI Advisor and outside AI apps are no longer confused:
  - **AI Assistant Config:** the provider, the key (relabelled "API Key (for AI Advisor)") and the model, with an intro saying it is billed to that key.
  - **Connect AI Apps (MCP):** in this order, the MCP switch, access level and confirmation, Remote Access (recommended), Claude and ChatGPT steps with copy-and-open buttons, and Discuss Shot with a note that it needs Remote Access.
  - The local, same-WiFi MCP setup moves into a dialog behind a small "Advanced…" link. It stays in settings search.
- User-visible text calls the feature "Remote Web Access" and the page "the web interface". It never says "Wasm", which is an implementation detail.

## Capabilities

### New Capabilities

- `remote-web-access`: Publishing the web interface through the embedded Tailscale Funnel behind a capability URL. It covers the safe URL and QR, token rotation, authenticator gating, route isolation and on/off semantics.

### Modified Capabilities

- `settings-ui`: The History & Data tab no longer holds the server toggle or authenticator controls. A new Network card in Connections holds the renamed "Enable Network" switch, the authenticator controls, the Tailscale node controls and the Remote Web Access controls.
- `mcp-server`: The remote listener and Tailscale node become shared between remote MCP and the remote web route. The isolated surface serves both tokenized routes and nothing else. Tailscale node setup and sign-out move out of the MCP settings, and disabling remote MCP stops the node only when the web route does not need it.

## Impact

- **Depends on** `replace-web-interface-with-wasm`, which provides the Wasm client served at `/`, the `/api/session/ws` transport and the same-origin package check. That change is still open, so this one lands after it or on top of it.
- QML: `SettingsConnectionsTab.qml` gains the Network card, probably as a new shared component file registered in both the native `CMakeLists.txt` and `src/webclient/CMakeLists.txt`. `SettingsHistoryDataTab.qml` loses the server and authenticator rows. `SettingsAITab.qml` loses the Tailscale node controls.
- Services: Tailscale node state and actions move from `AISettingsService` to a network-domain service. `ShotServerSettingsService` is extended or a sibling is added, following `SHARED_UI.md`.
- C++: `McpRemoteAccess` is generalised into one remote-access coordinator with two routes. ShotServer accepts in-process handoff of tunnel connections for the web route. Two new settings, `remoteWebEnabled` and `remoteWebToken`, go in `SettingsNetwork`.
- Settings search index regenerates. Translation keys move or are added.
- Wiki manual: update the Connections and History & Data pages and add a short "Remote Web Access" entry.
- Builds without `ENABLE_TSNET`, including Linux CI, show the Remote Web Access switch as unavailable, the same way the Tailscale MCP mode is shown today.
