## Context

Remote MCP access has a Tailscale ("built-in") mode backed by an embedded tsnet node (`McpTunnelTsnet`). The node persists its identity — nodekey, tailnet binding, certs — under `AppDataLocation/tsnet` (`McpRemoteAccess::startTunnel()`, `mcpremoteaccess.cpp:229`). Nothing in the running app ever clears that directory:

- `stopTunnel()` / disabling remote MCP calls `m_tunnel->stop()`, which only stops the worker and closes the libtailscale handle — the state dir survives.
- `rotateToken()` only rotates the MCP capability token; it does not touch the Tailscale identity.
- `McpTunnelTsnet::wipeState()` (`mcptunnel_tsnet.cpp:246`) already does exactly the right thing — `stop()` then `QDir(dir).removeRecursively()` — but no caller exists anywhere in `src/` or `qml/`.

When the stored nodekey belongs to a different account/tailnet than the one the user authorizes in, tsnet loops on `NeedsLogin` and Tailscale rejects authorization ("device already exists" / 403). On Android the state dir is app-private, so the only user-facing escape today is Clear Storage, which also wipes shots, profiles, and settings.

The settings UI (`SettingsAITab.qml`) already renders, inside the Tailscale block: the login URL/QR (shown when `RemoteMcpAccess.loginUrl` is non-empty), the connector URL/QR (when active), status text, and a Rotate Token button gated by a confirmation dialog. The confirmation-dialog pattern (`rotateTokenDialog`) is the template to reuse.

## Goals / Non-Goals

**Goals:**
- Give the user a one-tap, confirmation-gated "Sign out of Tailscale" action that wipes the tsnet identity.
- Make it reachable in the login-loop state (node up, `NeedsLogin`, no connector URL), which is the whole point.
- Reuse the existing `wipeState()` and the existing dialog pattern; add no new settings/properties.

**Non-Goals:**
- No automatic detection/self-heal of the "already exists"/403 condition — the user triggers sign-out explicitly.
- No change to token rotation, custom-URL mode, or disable semantics.
- No server-side device deletion (that stays a Tailscale admin-console action; the manual note covers it).

## Decisions

**1. Expose sign-out via a new `Q_INVOKABLE forgetTailscale()` on `McpRemoteAccess`, not by calling the tunnel from QML directly.**
`McpTunnelTsnet` is an internal (`m_tunnel`) that QML never sees; `McpRemoteAccess` is the QML-facing singleton (`RemoteMcpAccess`). The invokable guards for a null tunnel and delegates to `m_tunnel->wipeState()`. Rationale: keeps the QML surface consistent with `refresh()`/`rotateToken()` and keeps tunnel lifetime ownership in C++.
- *Alternative considered:* expose `McpTunnelTsnet` to QML. Rejected — widens the QML surface and leaks an implementation type.

**2. After wiping, re-evaluate settings rather than force a restart.**
`wipeState()` calls `stop()` internally. If remote MCP is still enabled in Tailscale mode, `forgetTailscale()` calls `refresh()` afterward so the coordinator brings a fresh node back up (new identity → new login URL) — matching the spec's "re-enabling requires a fresh login" without the user toggling anything. If the user wants it to stay down, they disable the toggle as usual.
- *Alternative considered:* leave it stopped and require a manual re-enable. Rejected — a dead node with no login URL after "sign out" reads as broken; a fresh login URL is the expected next step.

**3. Gate the button on `remoteMcpMode === "tailscale"` and tunnel availability, and show it whenever a node could exist — independent of `loginUrl`/`connectorUrl`.**
The button lives in the Tailscale block but is NOT nested under the `loginUrl.length > 0` or `connectorUrl.length > 0` sub-blocks (which are mutually exclusive and both hide it in some states). Placing it at the Tailscale-block level keeps it reachable during the login loop, when connected, and when reconnecting. Rationale: the failure mode we are fixing occurs precisely when there is a login URL but no connector URL.

**4. Confirmation dialog cloned from `rotateTokenDialog`.**
Same structure, destructive-action wording ("This clears this device's Tailscale identity. You'll need to sign in again to use remote access."), OK/Cancel, accessibility announce on open. New translation keys under `settings.ai.remoteMcp.tailscaleSignout.*`. Reuse `common.button.cancel`/`common.button.ok` where they fit.

## Risks / Trade-offs

- **[Wipe races an in-flight tsnet worker]** → `wipeState()` already calls `stop()` first, which joins the worker (up to its 12 s wait) before `removeRecursively()`. No new concurrency is introduced; the invokable runs on the main thread like the existing controls.
- **[User signs out while genuinely connected, losing a working setup]** → Confirmation dialog with explicit destructive wording; the connector token itself is unaffected, so after a fresh login the same claude.ai connector URL host may change (new node name is deterministic from device, so typically identical) — noted in the manual.
- **[Android state dir removal fails]** → `wipeState()` already logs a warning on `removeRecursively()` failure; acceptable — the far more common path (app-private dir owned by the app) succeeds. No new error surface needed for an edge that a Clear Storage still covers.
- **[Button visible in custom-URL mode]** → Guarded by `remoteMcpMode === "tailscale"` && `RemoteMcpAccess.tunnelAvailable`, mirroring the rest of the Tailscale block.

## Migration Plan

Additive, no data migration. Ships in the normal release. Rollback is reverting the commit — no persisted state format changes (it only *deletes* the tsnet dir, which the app already recreates on next enable).
