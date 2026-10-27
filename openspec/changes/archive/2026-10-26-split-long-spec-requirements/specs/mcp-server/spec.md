# Spec Delta

## MODIFIED Requirements

### Requirement: Reachability Mode — Embedded Tailscale Funnel

In Tailscale mode the app SHALL run an embedded tsnet node joining the tailnet and expose the remote surface via Tailscale Funnel at `https://<node>.<tailnet>.ts.net`. The node SHALL be shared by remote MCP and Remote Web Access, and SHALL run while either needs it. Setup SHALL surface the tsnet login URL (link and QR) and any Funnel-approval URL in the Connections tab's Network card.

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

## ADDED Requirements

### Requirement: Tailscale sign-out wipes the node identity
The Connections tab's Network card SHALL offer a confirmation-gated sign-out (forget) action that wipes the tsnet node state. It SHALL be reachable whenever a tsnet node exists, including before login.

#### Scenario: Sign-out offered before login
- **WHEN** a tsnet node exists but is still waiting for login
- **THEN** the Network card offers sign-out, and confirming it wipes the node state
