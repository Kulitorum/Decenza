## Why

The embedded Tailscale (Funnel) node persists its identity — including its nodekey — in an app-private state directory that the app never wipes on its own. When that stored identity no longer matches the account the user authorizes in (e.g. they first logged in under a different Tailscale account, or deleted/re-created the tailnet), the login enters an unrecoverable loop: Tailscale returns *"Device with nodekey … already exists; please log out explicitly"* and *"403: User is not authorized to view this auth request."* The `McpTunnelTsnet::wipeState()` method that would clear this already exists but is wired to nothing, and the spec's "forget action" was never implemented. On Android the state directory is app-private, so a user has no recovery path short of Clear Storage (which also erases shots, profiles, and settings).

## What Changes

- Add a **"Sign out of Tailscale"** action to the Remote MCP → Tailscale section of the AI settings, wired to the existing `McpTunnelTsnet::wipeState()` via a new `Q_INVOKABLE` on `McpRemoteAccess`.
- Make the action **reachable while the node is stuck in the login/NeedsLogin loop**, not only when the connector is Active — this is the primary case it must recover from.
- Guard the action with a **confirmation dialog** that states it clears the device's tailnet identity and requires a fresh login (consistent with the existing Rotate Token dialog pattern).
- After sign-out, the tsnet state is wiped and re-enabling Tailscale mode issues a **fresh login URL** (no stale nodekey).
- Update the user manual (wiki) with a short "stuck in a Tailscale login loop" recovery note.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `mcp-server`: Strengthen the existing "forget action" requirement under *Reachability Mode — Embedded Tailscale Funnel* to specify that the forget/sign-out action is surfaced in the settings UI, is reachable whenever a tsnet node exists (including the not-yet-authorized/login-loop state), is confirmation-gated, and that after it runs a subsequent enable triggers a fresh login rather than reusing the wiped identity.

## Impact

- **Code**: `src/mcp/mcpremoteaccess.h/.cpp` (new `Q_INVOKABLE forgetTailscale()` passthrough to `m_tunnel->wipeState()`), `qml/pages/settings/SettingsAITab.qml` (sign-out button + confirmation dialog in the Tailscale block), translation keys.
- **Existing code reused**: `McpTunnelTsnet::wipeState()` (already stops the node and removes the state dir).
- **No new settings/properties**, no data-model or DB changes, no BLE impact.
- **Docs**: GitHub wiki Manual — remote-access / Tailscale recovery note.
