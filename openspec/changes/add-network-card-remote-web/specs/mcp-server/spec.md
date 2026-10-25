# Spec Delta

## ADDED Requirements

### Requirement: Enable Network gates remote MCP
The remote MCP connector SHALL be served only while Enable Network is on, as the local MCP endpoint already is. Turning Enable Network off SHALL stop the MCP route and, when no other route needs them, the remote listener and the Tailscale node. The connector URL SHALL NOT be shown as usable while Enable Network is off.

#### Scenario: Network turned off
- **WHEN** remote MCP is on and the user turns Enable Network off
- **THEN** requests to `/mcp/<token>` are refused, the connector URL is not shown, and the AI tab says Remote Access needs Enable Network

#### Scenario: Network turned back on
- **WHEN** the user turns Enable Network back on
- **THEN** the same connector URL works again

## MODIFIED Requirements

### Requirement: Isolated Remote Surface

The remote reachability path SHALL terminate at a dedicated listener
that serves only the tokenized MCP route (`POST`, `GET`, `DELETE` on
`/mcp/<token>`) and, when Remote Web Access is enabled,
the tokenized web route defined by `remote-web-access`. All other paths
and methods on the remote surface SHALL return `404`. No other
ShotServer route (`/legacy/` HTML, data-migration API, or REST
endpoints outside the web route's allowance) SHALL be reachable through
the remote surface.

#### Scenario: Non-MCP route via tunnel
- **WHEN** a request arrives on the remote surface for any other path (e.g. `/layout`, `/api/shots`) without a valid web session
- **THEN** the server returns `404`

#### Scenario: Web route disabled
- **WHEN** Remote Web Access is off and a request arrives for `/web/<token>`
- **THEN** it is refused the same way as a wrong MCP token

#### Scenario: LAN surface unchanged
- **WHEN** a LAN client uses the existing local `/mcp` endpoint
- **THEN** behavior is unchanged by remote mode being enabled or disabled

### Requirement: Reachability Mode — Embedded Tailscale Funnel

In Tailscale mode the app SHALL run an embedded tsnet node joining the tailnet and expose the remote surface via Tailscale Funnel at `https://<node>.<tailnet>.ts.net`. The node SHALL be shared by remote MCP and Remote Web Access, and SHALL run while either needs it. Setup SHALL surface the tsnet login URL (link and QR) and any Funnel-approval URL in the Connections tab's Network card. A confirmation-gated sign-out (forget) action there SHALL wipe the tsnet node state, reachable whenever a tsnet node exists, even before login.

#### Scenario: First-time Tailscale setup
- **WHEN** the user selects Tailscale mode and enables remote MCP with no prior tsnet state
- **THEN** the Network card shows the tailnet login URL as link and QR and reports status until the node is authorized and Funnel is active

#### Scenario: Funnel active
- **WHEN** the tsnet node is authorized and Funnel is enabled
- **THEN** the AI tab displays the stable connector URL `https://<node>.<tailnet>.ts.net/mcp/<token>` and requests to it reach the remote surface

#### Scenario: Network change
- **WHEN** the device changes networks while Tailscale mode is active
- **THEN** the tsnet node reconnects automatically and the connector URL remains unchanged

#### Scenario: Forget tailnet
- **WHEN** the user invokes the sign-out action and confirms
- **THEN** the tsnet node is stopped, the tsnet state directory is wiped, and re-enabling requires a fresh tailnet login

#### Scenario: Sign out recovers a stuck login loop
- **WHEN** the node is waiting for login (a fresh login URL is shown) because its stored nodekey belongs to a different or deleted tailnet, so authorization keeps failing
- **THEN** the sign-out action is available in that state, and confirming it wipes the stored identity so the next enable produces a login that can succeed under the intended account

#### Scenario: Confirmation required
- **WHEN** the user invokes the sign-out action
- **THEN** a confirmation dialog is shown warning that the device's tailnet identity will be cleared, and the state is wiped only if the user confirms

#### Scenario: Forget warns before wiping
- **WHEN** the user invokes sign-out (forget)
- **THEN** a confirmation warns that the device's tailnet identity is cleared, that both remote MCP and the remote web interface stop working, and that re-enabling requires a fresh login

#### Scenario: Re-enable after forget
- **WHEN** Tailscale mode is re-enabled after forget
- **THEN** the node starts with no prior identity and issues a fresh login URL, without reusing the wiped nodekey

#### Scenario: Node shared with the web interface
- **WHEN** Remote Web Access is enabled and remote MCP is off
- **THEN** the node runs and the Funnel host serves only the web route

### Requirement: Remote MCP Disable Semantics

Disabling remote MCP SHALL stop serving the MCP route and terminate
remote MCP sessions. It SHALL stop the tunnel and close the remote
listener only when Remote Web Access is not enabled. The
capability token SHALL be retained so re-enabling does not invalidate
an already-configured connector.

#### Scenario: Toggle off
- **WHEN** the user disables remote MCP and Remote Web Access is off
- **THEN** in-flight remote sessions are closed, the remote listener stops, and the public URL ceases to resolve to the app

#### Scenario: Toggle off while the web route is on
- **WHEN** the user disables remote MCP while Remote Web Access is on
- **THEN** remote MCP sessions are closed and `/mcp/<token>` is refused, while the Funnel host keeps serving the web route

#### Scenario: Re-enable
- **WHEN** the user re-enables remote MCP in the same mode
- **THEN** the previous connector URL (same host, same token) works again without reconfiguring claude.ai
