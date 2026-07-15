## MODIFIED Requirements

### Requirement: Reachability Mode — Embedded Tailscale Funnel

In Tailscale mode, the app SHALL run an embedded tsnet node
(userspace, no system VPN interface) that joins the user's tailnet
and exposes the remote surface via Tailscale Funnel at a stable
`https://<node>.<tailnet>.ts.net` URL with a certificate managed by
Tailscale. Setup SHALL surface the tsnet login URL (link and QR) and
any required Funnel-approval URL. Disabling remote MCP SHALL bring
the tsnet listener down.

The app SHALL provide an explicit **sign-out (forget) action** in the
settings UI that wipes the tsnet node state directory. The action
SHALL be reachable whenever a tsnet node exists in Tailscale mode —
including while the node is unauthorized and waiting for login (the
state in which a stale nodekey produces an unrecoverable login loop),
not only when the connector is active. The action SHALL be
confirmation-gated, warning that it clears this device's tailnet
identity and that re-enabling requires a fresh login. After the action
runs, re-enabling Tailscale mode SHALL bring the node up with no prior
identity and issue a fresh tailnet login URL rather than reusing the
wiped nodekey.

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
- **WHEN** the user invokes the sign-out action and confirms
- **THEN** the tsnet node is stopped, the tsnet state directory is wiped, and re-enabling requires a fresh tailnet login

#### Scenario: Sign out recovers a stuck login loop
- **WHEN** the node is waiting for login (a fresh login URL is shown) because its stored nodekey belongs to a different or deleted tailnet, so authorization keeps failing
- **THEN** the sign-out action is available in that state, and confirming it wipes the stored identity so the next enable produces a login that can succeed under the intended account

#### Scenario: Confirmation required
- **WHEN** the user invokes the sign-out action
- **THEN** a confirmation dialog is shown warning that the device's tailnet identity will be cleared, and the state is wiped only if the user confirms
