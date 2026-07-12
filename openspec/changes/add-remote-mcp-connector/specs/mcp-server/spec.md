## ADDED Requirements

### Requirement: Capability-URL Authorization for Remote Access

When remote MCP is enabled, the remote surface SHALL authorize
requests solely by an unguessable capability token carried as a URL
path segment (`/mcp/<token>`), where the token is a 128-bit
cryptographically random value generated on-device. Token comparison
SHALL be constant-time. Requests with a missing or non-matching token
SHALL receive a bare HTTP `404` that does not reveal that an MCP
server exists.

#### Scenario: Valid token
- **WHEN** a client POSTs a JSON-RPC request to `/mcp/<token>` with the current token
- **THEN** the request is dispatched to the MCP server and handled normally

#### Scenario: Wrong token
- **WHEN** a client POSTs to `/mcp/<other>` where `<other>` is not the current token
- **THEN** the server returns `404` with no MCP-identifying headers or body

#### Scenario: Missing token
- **WHEN** a client POSTs to `/mcp` on the remote surface
- **THEN** the server returns `404`

### Requirement: Token Rotation as Revocation

The settings UI SHALL provide a rotate-token action. Rotation SHALL
generate a fresh token, immediately close all active remote MCP
sessions, and cause requests bearing the previous token to receive
`404`. The UI SHALL display the new connector URL and QR code after
rotation.

#### Scenario: User rotates the token
- **WHEN** the user confirms the rotate-token action
- **THEN** a request using the old token returns `404` within one second and active remote sessions are terminated

#### Scenario: New URL shown
- **WHEN** rotation completes
- **THEN** the settings UI displays the connector URL containing the new token, with copy and QR affordances

### Requirement: Isolated Remote Surface

The remote reachability path SHALL terminate at a dedicated listener
that serves only the tokenized MCP route (`POST`, `GET`, `DELETE` on
`/mcp/<token>`). All other paths and methods on the remote surface
SHALL return `404`. No other ShotServer route (web layout editor,
REST endpoints, data-migration API) SHALL be reachable through the
remote surface.

#### Scenario: Non-MCP route via tunnel
- **WHEN** a request arrives on the remote surface for any other path (e.g. `/layout`, `/api/shots`)
- **THEN** the server returns `404`

#### Scenario: LAN surface unchanged
- **WHEN** a LAN client uses the existing local `/mcp` endpoint
- **THEN** behavior is unchanged by remote mode being enabled or disabled

### Requirement: Remote Sessions Honor Existing MCP Gates

Remote MCP sessions SHALL be subject to the same `mcpAccessLevel`
filtering, `mcpConfirmationLevel` confirmation flows (including the
in-app dialog for machine-start operations), session limits, and
rate limits as LAN sessions.

#### Scenario: Access level enforced remotely
- **WHEN** `mcpAccessLevel` is Monitor Only and a remote client calls a control-category tool
- **THEN** the call is rejected identically to the LAN behavior

#### Scenario: In-app confirmation over the tunnel
- **WHEN** `mcpConfirmationLevel` requires confirmation and a remote client calls `machine_start_espresso`
- **THEN** the on-device confirmation dialog is shown and the held response resolves per the user's choice or the dialog timeout

### Requirement: Failed-Token Rate Limiting

The remote surface SHALL rate-limit requests that fail token
validation, per source, and SHALL log failures without echoing the
attempted path.

#### Scenario: Repeated bad tokens
- **WHEN** a source exceeds the failed-token limit
- **THEN** further requests from that source are dropped or delayed for the limit window

### Requirement: Reachability Mode — Embedded Tailscale Funnel

In Tailscale mode, the app SHALL run an embedded tsnet node
(userspace, no system VPN interface) that joins the user's tailnet
and exposes the remote surface via Tailscale Funnel at a stable
`https://<node>.<tailnet>.ts.net` URL with a certificate managed by
Tailscale. Setup SHALL surface the tsnet login URL (link and QR) and
any required Funnel-approval URL. Disabling remote MCP SHALL bring
the tsnet listener down; an explicit forget action SHALL wipe the
tsnet node state.

#### Scenario: First-time Tailscale setup
- **WHEN** the user selects Tailscale mode and enables remote MCP with no prior tsnet state
- **THEN** the UI shows the tailnet login URL as link and QR and reports status until the node is authorized and Funnel is active

#### Scenario: Funnel active
- **WHEN** the tsnet node is authorized and Funnel is enabled
- **THEN** the UI displays the stable connector URL `https://<node>.<tailnet>.ts.net/mcp/<token>` and requests to it reach the remote surface

#### Scenario: Network change
- **WHEN** the device changes networks while Tailscale mode is active
- **THEN** the tsnet node reconnects automatically and the connector URL remains unchanged

#### Scenario: Forget tailnet
- **WHEN** the user invokes the forget action
- **THEN** the tsnet state directory is wiped and re-enabling requires a fresh tailnet login

### Requirement: Reachability Mode — Bring-Your-Own URL

In custom-URL mode, the user SHALL provide an `https://` base URL
that they have arranged to forward to the device's remote listener.
The UI SHALL compose and display the full connector URL
(`<base>/mcp/<token>`) with copy and QR affordances. The app SHALL
NOT attempt to manage the user's tunnel.

#### Scenario: Custom URL configured
- **WHEN** the user enters `https://coffee.example.ts.net` as the base URL
- **THEN** the UI displays connector URL `https://coffee.example.ts.net/mcp/<token>` with copy and QR

#### Scenario: Non-HTTPS rejected
- **WHEN** the user enters an `http://` base URL
- **THEN** the value is rejected with a validation message

### Requirement: Remote Access Status Visibility

The settings UI SHALL show the live state of the remote surface
(`off`, `starting`, `active`, `reconnecting`, `error`) and SHALL NOT
display the connector URL as usable while the underlying tunnel is
down.

#### Scenario: Tunnel drops
- **WHEN** the active tunnel disconnects
- **THEN** the status changes from `active` to `reconnecting` (or `error`) and the UI reflects that the URL is currently unreachable

### Requirement: Remote MCP Disable Semantics

Disabling remote MCP SHALL stop all tunnels, close the remote
listener, and terminate remote sessions. The capability token SHALL
be retained so re-enabling does not invalidate an already-configured
connector.

#### Scenario: Toggle off
- **WHEN** the user disables remote MCP
- **THEN** in-flight remote sessions are closed, the remote listener stops, and the public URL ceases to resolve to the app

#### Scenario: Re-enable
- **WHEN** the user re-enables remote MCP in the same mode
- **THEN** the previous connector URL (same host, same token) works again without reconfiguring claude.ai
