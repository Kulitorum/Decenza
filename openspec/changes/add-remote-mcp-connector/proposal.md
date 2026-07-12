# Change: Zero-infrastructure remote MCP for Claude / ChatGPT mobile connectors

## Why

Decenza's MCP server listens on plain HTTP on the LAN. Claude
(claude.ai / Claude mobile) and ChatGPT custom connectors dial the MCP
endpoint **from the vendor's cloud backend, not from the user's
phone** — so the endpoint must be reachable on the public internet
over HTTPS with a valid certificate. Today the only bridge is
`mcp-remote` on a desktop on the same LAN, which excludes the mobile
apps entirely.

Two constraints shape this revision (superseding the earlier draft of
this change):

1. **No developer-run service.** Decenza is a free app; a relay
   (previous Option A) adds ongoing cost and operational burden.
   Every reachability option must run on-device or on infrastructure
   the *user* owns (free tiers of Tailscale / ngrok, or their own
   tunnel).
2. **OAuth is not actually required.** Verified against claude.ai's
   connector documentation: the OAuth client ID/secret are *optional*
   advanced settings. A server that never returns a `401` challenge is
   connected unauthenticated. That removes the largest work item of
   the earlier draft (an on-device OAuth 2.1 AS with DCR and
   Google/Microsoft OIDC federation) in favour of a capability URL.

The MCP protocol side is already done: the server speaks Streamable
HTTP at protocol `2025-11-25` (with `2025-06-18` / `2025-03-26`
fallbacks) since change `update-mcp-protocol-2025-11-25`.

## What Changes

- **Capability-URL authentication.** A remote request is authorized by
  an unguessable path segment: `https://<host>/mcp/<token>` where
  `<token>` is a 128-bit random value generated on-device. Wrong or
  missing token → `404` (indistinguishable from no server). The user
  can rotate the token at any time from Settings (rotation is the
  revocation story). Comparison is constant-time; failed attempts are
  rate-limited per source.
- **Isolated remote surface.** Remote reachability modes forward to a
  dedicated loopback listener that serves **only** the tokenized MCP
  route. The rest of ShotServer (web layout editor, REST endpoints,
  data-migration API) is never exposed publicly.
- **Three reachability modes**, selectable in Settings → Remote MCP.
  Mode A is the primary, flagship path of this change (the device
  owner driving this change already runs a tailnet, and it is the
  strongest zero-cost option overall); Mode C is a near-free fallback
  that shares all the core work; Mode B is deferred until demand and
  a compatibility check justify it:

  **Mode A — Embedded Tailscale (tsnet) + Funnel (primary).**
  Embed `tsnet`/libtailscale (the library the official Android client
  is built on) via gomobile. It runs in userspace (its own netstack —
  no `VpnService`, no conflict with other VPNs, no root). The app
  joins the user's tailnet as its own node and calls `ListenFunnel()`
  to get a stable public HTTPS URL
  (`https://decenza.<tailnet>.ts.net/mcp/<token>`) with a Let's
  Encrypt cert provisioned and renewed by Tailscale. Free plan,
  no domain purchase, CGNAT-immune, no bandwidth-metered tier.
  Cost: ~20–30 MB APK size for the Go runtime; Funnel is still
  labeled beta; one-time account + funnel-attribute approval.

  **Mode B — Embedded ngrok (deferred).** Embed the ngrok agent SDK
  (`ngrok-java`, Android-compatible). User pastes the authtoken from
  their free ngrok account; the free tier includes one static domain,
  so the connector URL is stable. Smaller binary than tsnet and not
  beta; caveats are free-tier bandwidth limits and the browser-UA
  interstitial (must verify Anthropic's backend passes through —
  non-browser user agents do). Not scheduled until Mode A has shipped
  and users without a tailnet ask for an alternative.

  **Mode C — Bring-your-own URL (fallback).** A settings field for
  a public base URL plus documentation. Users who already run
  Tailscale, cloudflared, or any reverse proxy on *any* box at home
  (the tunnel need not run on the tablet — it forwards to the
  tablet's LAN IP) can connect Claude mobile with zero new
  infrastructure code. This mode is also the escape hatch for
  platforms where embedding a tunnel is impractical.

- **Settings UI** (Remote MCP section):
  - Enable toggle + mode selector (Tailscale / ngrok / Custom URL).
  - Connection status and the full connector URL, with a copy button
    and a QR code for configuring the connector on claude.ai.
  - "Rotate token" action (invalidates the old URL immediately).
  - Mode-specific setup: Tailscale login link/QR + Funnel approval
    status; ngrok authtoken + static domain fields; custom base URL
    field.
- **Access control**: the existing `mcpAccessLevel` /
  `mcpConfirmationLevel` settings govern remote sessions exactly as
  they do LAN sessions, including the in-app confirmation dialog for
  machine-start operations. Per-client scopes are deferred (without
  OAuth there is no client identity to bind them to).

### Explicitly removed from the earlier draft

- **Decenza relay on `api.decenza.coffee`** (violates constraint 1).
- **On-device OAuth 2.1 AS, Dynamic Client Registration,
  Google/Microsoft OIDC federation** (unnecessary per constraint 2).
- **DuckDNS + UPnP + Let's Encrypt direct exposure** — rejected:
  25–45% silent failure on CGNAT/disabled UPnP, ~1 week of ACME work,
  and it makes the device itself internet-facing. Modes A/B achieve
  the same result with none of that. Revisit only on user demand.
- **Protocol version bump** — already shipped separately.

## Impact

- **Affected specs**: `mcp-server` (remote-connector concerns only).
- **Affected code**:
  - `src/mcp/mcpremoteaccess.{h,cpp}` — new: coordinator (token
    lifecycle, mode switching, remote-only loopback listener / route
    gating, status signals for QML).
  - `src/mcp/mcptunnel_tsnet.{h,cpp}` + `libs/tsnet-wrapper/` — new:
    thin JNI/gomobile wrapper around a small Go library exposing
    up/down/status/loginURL/funnel state.
  - `src/mcp/mcptunnel_ngrok.{h,cpp}` — new: ngrok SDK binding.
  - `src/network/shotserver.cpp` — route gating for the remote
    listener (only `/mcp/<token>` served).
  - `src/core/settings_network.{h,cpp}` — new properties (enabled,
    mode, token, ngrok authtoken/domain, custom URL); token and
    authtoken stored via QtKeychain where available.
  - `qml/pages/settings/SettingsAITab.qml` (or new `RemoteMcpTab.qml`)
    + QR code display component.
  - `android/` — gradle packaging for the tsnet AAR (Mode A).
- **Not affected**: no changes to any backend repo; no new cloud
  infrastructure; LAN `mcp-remote` path unchanged.
- **Security**: the public surface is a single tokenized MCP route
  behind the tunnel vendor's TLS. The token-in-URL trade-off (URLs
  appear in logs) is accepted for this threat model and mitigated by
  rotation, rate limiting, and the existing access-level +
  confirmation gates. See design.md.
- **User impact**: existing LAN/`mcp-remote` users unaffected. Remote
  mode is opt-in and defaults off.
