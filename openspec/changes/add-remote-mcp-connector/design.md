## Context

The MCP server (`src/mcp/mcpserver.cpp`) speaks Streamable HTTP at
protocol `2025-11-25` on the LAN with no auth. Claude and ChatGPT
mobile apps can only use a "custom connector" whose URL is fetched
**by the vendor's cloud backend** — the phone never dials the server
directly. So LAN reachability, tailnet-only reachability, and
phone-side VPNs are all irrelevant; the endpoint must be public HTTPS
with a valid certificate.

Hard constraints for this revision:

1. **Zero developer-run infrastructure.** Decenza is free; nothing in
   this change may create recurring cost or an operations burden.
2. Primary deployment target is **Decenza on an Android tablet**
   (typically a dedicated, plugged-in, screen-on device next to the
   machine).

## Goals / Non-Goals

- **Goals**
  - Claude mobile (iOS/Android) and ChatGPT mobile can add Decenza as
    a custom connector with no desktop intermediary and no
    developer-run service.
  - Stable connector URL (survives app restart, reboot, IP change,
    CGNAT).
  - No regression for LAN / `mcp-remote` users.
  - User can revoke remote access instantly.
- **Non-Goals**
  - OAuth / per-client identity and scopes. Without an AS there is one
    principal: whoever holds the capability URL.
  - Multi-user / multi-tenant auth.
  - A general-purpose reverse tunnel. The public surface is the MCP
    route only.

## Decisions

### 1. Capability URL instead of OAuth

claude.ai custom connectors treat OAuth as optional: if the server
never returns a `401` challenge, the connector runs unauthenticated.
(OAuth client ID/secret are optional "advanced settings"; verified
against Anthropic's connector docs, July 2026.)

So authorization is an unguessable URL path:

```
https://<host>/mcp/<token>        token = 128-bit CSPRNG, base64url
```

- Wrong/missing token → `404` with no body that reveals an MCP server
  exists. Constant-time comparison. Per-source rate limit on failures
  (brute force is cryptographically hopeless at 128 bits; the limit
  is defense-in-depth and log hygiene).
- **Rotation is revocation**: Settings has one "rotate" action; the
  old URL dies instantly, the UI shows the new URL/QR and reminds the
  user to update the connector on claude.ai.
- Trade-off, stated honestly: the MCP auth spec discourages secrets
  in URLs because URLs land in logs and proxies. Accepted here
  because (a) the alternative — an internet-facing OAuth 2.1 AS
  written in Qt/C++ — is a far larger attack surface than a static
  token, (b) the TLS hop is end-to-end through the tunnel vendors so
  only Anthropic's backend and the tunnel vendor see the path, and
  (c) blast radius is bounded by `mcpAccessLevel` and the in-app
  confirmation dialog for machine-start operations.
- Existing LAN behavior (`/mcp`, no token) is unchanged and never
  reachable through the remote surface.

**Alternative rejected — on-device OAuth 2.1 AS + IdP federation**
(the earlier draft): ~2+ weeks of security-critical C++, DCR, JWKS,
consent flows… none of it required by the connector platforms for a
single-owner device. If per-client scopes are ever genuinely needed,
revisit; the capability-URL scheme doesn't preclude adding OAuth
later (a `401` challenge simply starts being emitted).

### 2. Isolated remote surface

Tunnels terminate at a **dedicated loopback listener** (its own port,
owned by `McpRemoteAccess`) that routes *only*
`POST/GET/DELETE /mcp/<token>` and returns `404` for everything else.
ShotServer's web editor, REST API, and data-migration endpoints are
never exposed publicly, and a future ShotServer route can't
accidentally leak into the remote surface. The listener forwards
in-process to the same `McpServer` dispatch the LAN path uses, with a
session flag marking the session remote (for the status UI and rate
limiting).

### 3. Reachability: embedded tsnet + Funnel (A, primary), embedded ngrok (B, deferred), BYO URL (C, fallback)

Mode A is the flagship: the driving user already runs Tailscale, so
account friction is zero for them, and it is the strongest option on
merits (stable free URL, managed certs, CGNAT-immune, no metered
tier). Mode C ships alongside because it costs almost nothing beyond
the shared core. Mode B waits for demand.

**Mode A — tsnet/libtailscale + Funnel (primary).**
The earlier draft ruled out "Tailscale Funnel on Android" — correct
for the *Tailscale app* (still true; tailscale/tailscale#17071 open),
wrong for the *library*. `tsnet` embeds via gomobile and is exactly
what the official Android client is built on:

- Userspace netstack: no `VpnService`, so no conflict with a user's
  real VPN, no root, and no Android VPN-slot fight.
- `ListenFunnel()` yields a public HTTPS listener at
  `https://<node>.<tailnet>.ts.net` — stable name, CGNAT-immune,
  cert issued/renewed by Tailscale, free plan, no metered tier.
- Login flow: tsnet surfaces an auth URL → show as QR/link; user
  approves once. Funnel additionally needs the one-time
  funnel-attribute approval, also surfaced as a URL.
- Funnel constraint: public ports limited to 443/8443/10000 (we need
  one); throughput is modest (MCP JSON is tiny).
- Costs: ~20–30 MB APK (Go runtime), Funnel is beta, the Go wrapper
  is a new build artifact (small Go lib → AAR via gomobile, checked
  into `libs/` or built in CI).

**Mode A is multi-platform.** One Go wrapper codebase, per-platform
build outputs:

| Platform | Build output | Notes |
|---|---|---|
| Android | AAR via gomobile | Primary target, ships first |
| Windows/macOS/Linux | `c-shared` library (`.dll`/`.dylib`/`.so`) | Plain cgo; tsnet's userspace netstack coexists with an installed Tailscale client (no TUN conflict). Desktop users with the official client can alternatively just run `tailscale funnel` themselves = Mode C |
| iOS | xcframework via gomobile | Feasible (the official iOS app is built on libtailscale); no Network Extension entitlement needed since we don't route device traffic, so its memory cap doesn't apply. Sequenced last |

The C API surface (`Up/Down/Status/LoginURL/FunnelURL`) is identical
everywhere; only packaging differs. CI builds the artifacts per
platform (Go toolchain added to the relevant workflows).

**Mode B — ngrok agent SDK (deferred).** `ngrok-java` runs on Android. Free
account + free static domain → stable URL, TLS at ngrok's edge.
Smaller than tsnet, not beta. Caveats: free-tier bandwidth caps
(fine for JSON), and ngrok's browser-warning interstitial — served
only to browser-like user agents; **must verify during
implementation** that Anthropic's MCP client passes through, else
Mode B ships disabled.

**Mode C — BYO public URL (fallback).** A base-URL setting + docs.
Covers power users (existing Tailscale/cloudflared on any home box,
forwarding to the tablet's LAN IP), covers iOS/desktop builds of
Decenza, and is the fallback whenever A breaks. Nearly free: the
remote listener + token work is shared, the "tunnel" is the user's
problem. It also unblocks testing the whole flow before the tsnet
wrapper lands — a Funnel on any machine on the same tailnet can
front the tablet in the meantime.

**Rejected reachability options:**

- **Decenza relay** (`api.decenza.coffee` + Lambda/DynamoDB bridge):
  violates the no-developer-infrastructure constraint. (A Cloudflare
  Workers free-tier relay would be $0/month but is still
  developer-operated; rejected for ops burden, kept in mind as a
  future premium feature.)
- **DuckDNS + UPnP + ACME direct exposure**: 25–45% silent failure
  (CGNAT / UPnP disabled), ~1 week of ACME work, device itself
  becomes internet-facing. Strictly dominated by A/B.
- **Cloudflare quick tunnels**: free and accountless but the URL
  rotates per launch — useless for a connector configured once on
  claude.ai. (Named tunnels require the user to own a domain; that
  recipe goes in the Mode C docs instead.)
- **DuckDNS + Cloudflare combined**: structurally impossible —
  Cloudflare tunnels/proxying require the hostname's zone in the
  user's Cloudflare account; `duckdns.org` isn't delegable, and
  DuckDNS can't point at an ephemeral quick-tunnel host (SNI/Host
  routing, no cert).
- **Tailscale Android app alongside Decenza**: still no Funnel
  support in the app; requires the VPN slot; rejected.

### 4. Access control and confirmations unchanged

`mcpAccessLevel` (read/control/full) and `mcpConfirmationLevel` apply
to remote sessions identically to LAN sessions — same tool-dispatch
gates, same in-app `McpConfirmDialog` for machine-start operations
(the held-response pattern works through tunnels; 15 s dialog timeout
is well under tunnel idle timeouts). Remote sessions count toward the
same session and rate limits.

### 5. Android reliability

The tunnel client lives in the app process. Decenza tablets are
typically plugged in and screen-on (the app already keeps the screen
awake), so Doze is a corner case, not the common case. Still:

- Run tunnel maintenance off the main thread; reconnect with backoff
  on network change (`QNetworkInformation`).
- Surface a clear status ("Public URL active / reconnecting / off")
  in Settings; never pretend the URL works while the tunnel is down.
- If the app is backgrounded and Android kills connectivity, the
  connector fails closed (Anthropic's backend gets a timeout). No
  FCM wake-up is possible without a relay — documented limitation.

## Risks / Trade-offs

- **Token in URL** → mitigations in Decision 1; document plainly.
- **APK size (+20–30 MB, Mode A)** → gate the tsnet dependency so
  builds without it remain possible; evaluate Play Store dynamic
  feature module if size complaints materialize.
- **Funnel is beta / Tailscale policy changes** → Mode B and C exist;
  mode switching is a settings change, not a migration.
- **ngrok interstitial or free-tier changes** → verify early (task
  4.1); Mode B is optional to ship.
- **Vendor account friction** (Tailscale/ngrok sign-up) → one-time,
  QR-assisted; still far less friction than DuckDNS+UPnP or a
  desktop bridge.
- **Protocol churn on the connector side** → the MCP server already
  negotiates three protocol versions; nothing here touches protocol.

## Migration Plan

1. Ship the shared core (capability token + isolated listener) with
   Mode C (BYO URL) behind the `remoteMcpEnabled` toggle (default
   off) — this immediately enables the Tailscale-on-another-box
   setup, so the end-to-end Claude-mobile flow is testable on a real
   tailnet before any Go code lands.
2. Ship Mode A (embedded tsnet + Funnel) as the headline feature.
3. Mode B (ngrok) only after the interstitial check passes and
   non-tailnet users ask for it.
3. Rollback = toggle off: tunnels stop, remote listener closes,
   token stays stored. No data migration in either direction.
4. The earlier draft's relay/OAuth work was never started; nothing to
   unwind.

## Open Questions

- Package tsnet wrapper as a prebuilt AAR checked into the repo vs
  built in CI (Go toolchain in the Android workflow)?
- Should rotate-token also be exposed as an MCP tool (self-serve
  "lock out other clients"), or Settings-only? Leaning Settings-only.
- Do we want a second, read-only token (share "monitor my machine"
  without control)? Deferred; needs per-token access levels.
