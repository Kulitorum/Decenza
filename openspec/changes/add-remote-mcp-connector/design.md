## Context

The existing MCP server (`src/mcp/mcpserver.cpp`) speaks Streamable
HTTP + SSE on the LAN with no auth. Users reach it from Claude Desktop
via the `mcp-remote` bridge. Claude and OpenAI mobile apps cannot use
`mcp-remote`; their "custom connector" UIs expect a remote MCP URL that
is HTTPS, speaks Streamable HTTP, and authenticates via OAuth 2.1 with
PKCE and Dynamic Client Registration (DCR), per the current MCP auth
spec.

Two hard problems: (a) an espresso machine on a home Wi-Fi is not
publicly reachable, (b) running an OAuth AS inside a Qt app is
non-trivial.

## Goals / Non-Goals

- **Goals**
  - Claude mobile and ChatGPT mobile can add Decenza as a custom
    connector with no desktop intermediary.
  - No regression for existing `mcp-remote` users.
  - User controls what scopes each client gets; can revoke.
- **Non-Goals**
  - Multi-user / multi-tenant auth. The device owner is the only
    principal.
  - Federation with external IdPs (Google, Apple). The device *is* the
    AS.
  - A general-purpose reverse-tunnel product. Relay is narrow: MCP
    JSON-RPC only.

## Decisions

### 1. Decenza is its own OAuth Authorization Server, federating to Google/Microsoft for identity

Claude and ChatGPT mobile connectors require Dynamic Client
Registration (RFC 7591). Google and Microsoft do not expose open DCR —
apps must be pre-registered per client in their developer console. So
we cannot simply point connectors at `accounts.google.com`. Decenza
therefore runs its own AS, but defers *identity* to Google/Microsoft
via standard OIDC.

Alternatives considered:

- **Fully managed AS that supports DCR + social login** (Auth0, Clerk,
  WorkOS, Keycloak). Cleanest security story but mandates a cloud
  account for every user even for LAN use, and DCR is a paid tier on
  most providers.
- **Decenza AS with a device-only passcode, no IdP**. Simplest, but
  makes the tablet itself the only auth factor — a stolen tablet is a
  stolen machine. Rejected in favor of mandatory IdP login.
- **Chosen**: Decenza AS + mandatory Google/Microsoft OIDC at consent
  time. No passwords stored on device, stolen-tablet risk is bounded
  by the IdP account.

**Flow**: `/authorize` → Decenza shows "Sign in with Google /
Microsoft" → browser completes OIDC auth code + PKCE against the IdP →
Decenza verifies the `id_token` signature via the IdP's JWKS, checks
`iss`, `aud`, `exp`, and matches `sub` (preferred) or `email` against
the configured device owner → shows consent dialog on device → on
approve, issues Decenza's own access token (unrelated to the IdP
token).

JWT access tokens signed with a per-device Ed25519 key; public key
published at the AS metadata endpoint. Refresh tokens are opaque and
stored server-side (on the device) so revoke is immediate. IdP tokens
are verified and discarded — Decenza does not hold IdP refresh tokens.

**Non-goal for this change**: a device-only passcode fallback for
users who refuse a Google/Microsoft account. Deferred until a user
actually asks.

### 2. Two reachability options: relay (Option A) and DuckDNS+UPnP (Option B)

Connector URLs are fetched by Anthropic/OpenAI's cloud backends, not
by the user's phone — so the URL must be publicly reachable. Tailscale
Funnel and Cloudflare Tunnel are ruled out on Android (Decenza's
primary platform). Two options ship in this change:

**Option A — Decenza relay (recommended)**
Extend the existing `decenza-shotmap` backend (`api.decenza.coffee`).
The device keeps its existing outbound WebSocket; the relay exposes an
HTTP endpoint that bridges MCP JSON-RPC to the WebSocket using a
DynamoDB correlation-ID pattern. The OAuth AS runs on the relay
(Lambda) and validates tokens using the device's stored public key —
the device never runs an internet-facing HTTP listener.

Pros: works on Android and all other platforms regardless of NAT/CGNAT;
infrastructure already exists and is maintained; near-zero incremental
cost on the existing Lambda stack; device attack surface is zero.

Cons: dependency on `api.decenza.coffee` uptime (LAN path stays
functional if relay is down); dual-repo change (Decenza + shotmap).

**Option B — DuckDNS + UPnP + Let's Encrypt (on-device)**
For users who prefer no relay dependency. Device handles DDNS, UPnP
port mapping, ACME cert issuance, and runs the OAuth AS and HTTPS
listener directly.

Pros: no external dependency, fully self-contained.

Cons: ~25–45% of users will be behind CGNAT or have UPnP disabled
and silently fail; our OAuth AS is directly internet-facing; ACME
renewal logic is ~1 week of additional work; security surface is larger.

**Why DuckDNS** (over No-IP / Dynu / others): its TXT-update API is a
single GET call (`/update?domains=...&token=...&txt=...`), requires no
SDK, and has been the standard ACME DNS-01 backend for home users for
~a decade.

The relay architecture variant `AS-on-Relay` noted previously is now
the actual Option A design — not a variant.

### 3. IdP sign-in in the browser, scope consent on the device

The OAuth `/authorize` endpoint redirects the connector's browser tab
to Google/Microsoft for sign-in. On return, Decenza verifies the
`id_token` and then parks the flow, showing "Check your Decenza
device screen to approve" in the browser while a QML dialog on the
machine asks the owner to allow/deny the requested scopes. This splits
the two concerns cleanly: the browser handles identity (where the
user expects to type their Google/Microsoft password), the device
handles authorization (where the user can physically see their
espresso machine).

Client registration metadata (`client_id` → Google/Microsoft OAuth app)
ships with the build; per-user IdP secrets are not required because we
use the PKCE public-client flow against the IdP.

**Cost**: both IdPs are free for our usage. Google Cloud OAuth clients
cost nothing to create; requesting only `openid`/`email`/`profile`
avoids Google's "sensitive scope" verification process entirely and
has no user cap in production. Microsoft Entra app registration is
free, including multi-tenant apps that accept personal accounts via
the `common` endpoint. The only ongoing obligation is keeping the
OAuth consent-screen metadata (homepage URL, privacy policy URL)
current.

### 4. Scope model mirrors existing access levels

We already have `Settings::mcpAccessLevel` (0=read, 1=control, 2=full).
Map 1:1 to `mcp:read`, `mcp:control`, `mcp:full`. No new authorization
model; OAuth just gates which level a given client is allowed to ask
for.

## Risks / Trade-offs

- **CGNAT excludes some users entirely** → Detect at setup and refuse
  clearly; point to the future paid relay.
- **UPnP is flaky** → First-class error messages when mapping fails;
  document manual port forwarding as a follow-up.
- **Direct internet exposure of the AS** → Narrow surface (PKCE-only,
  no passwords, no implicit), rate limiting on `/oauth/*` and `/mcp`,
  unit tests per RFC 7636 §4.6 vectors, external review before ship.
- **ACME renewal failure silently breaks remote MCP** → Alert in the
  UI 20+ days before expiry if renewal hasn't succeeded.
- **Token storage on a jailbroken/rooted device** → QtKeychain where
  available; document that remote MCP is only as secure as the device.
- **Protocol churn** → MCP auth spec is young. Keep version negotiation
  permissive and log unknown fields rather than rejecting.

## Migration Plan

1. Ship behind a feature flag (`remoteMcpEnabled`, default off).
2. Beta with willing users via the existing pre-release channel.
3. GA after one release cycle with no auth-related bugs.
4. `mcp-remote`/LAN path remains supported indefinitely — it's the
   local dev story.

Rollback: flipping `remoteMcpEnabled` off releases the UPnP mapping,
rebinds the listener to loopback, and stops DDNS updates. No data
migration.

## Open-Source Dependencies

| Concern | Library | License | Role |
|---|---|---|---|
| JWT sign/verify, JWKS | [`jwt-cpp`](https://github.com/Thalhammer/jwt-cpp) (header-only) | MIT | Access-token signing (Ed25519), IdP `id_token` verification, JWKS caching |
| OIDC client (Google/Microsoft PKCE) | `QtNetworkAuth` (`QOAuth2AuthorizationCodeFlow`) | LGPLv3 / commercial (already in our Qt install) | Browser-based sign-in against the IdP |
| WebSocket (relay) | `QWebSocket` (Qt Network) | LGPLv3 / commercial (already in) | Outbound WSS to relay |
| Reference AS (if relay on Workers) | [`cloudflare/workers-oauth-provider`](https://github.com/cloudflare/workers-oauth-provider) | MIT | Off-device AS covering OAuth 2.1 + RFC 8414/9728/7591 + CIMD — used only if the relay also terminates auth |
| Reverse tunnel (BYO) | [Cloudflare Tunnel](https://github.com/cloudflare/cloudflared) / Tailscale Funnel | Apache-2.0 / BSD-3 | User-side tunnel for `tunnel` mode; no code in our repo |
| Test vectors | RFC 7636 §4.6 (PKCE), OIDF conformance suite | — | Validate our AS |

Nothing in the C++/Qt ecosystem implements an embeddable OAuth 2.1 AS
with DCR/CIMD — verified via a spec-level survey as of 2026. We write
that piece; `jwt-cpp` removes the crypto work. The existing
`hkr04/cpp-mcp` and `GopherSecurity/gopher-mcp` C++ SDKs do not cover
the auth surface, so we continue with our in-tree `McpServer`.

## Architecture Variant: AS-on-Relay

A late-discovered alternative is to run the OAuth AS **at the relay**
rather than on the device, using `workers-oauth-provider` on Cloudflare
Workers. The device becomes a pure resource server: it only validates
bearer tokens (JWT verified against the relay's JWKS) and enforces
scope → access-level mapping. All of §2 (OAuth AS endpoints,
registration, consent redirect) moves off-device.

Trade-offs vs. on-device AS:

- **Pro**: removes the riskiest C++ code from the device; leverages a
  maintained OSS AS; consent can still be proxied to the device via a
  polling endpoint.
- **Pro**: aligns with where DCR/CIMD traffic naturally lands (public
  HTTPS).
- **Con**: hard dependency on the Decenza relay for authentication —
  the BYO-tunnel mode would need to ship its own AS or fall back to
  CIMD-only with a pre-registered Claude/OpenAI client list.
- **Con**: Cloudflare Workers lock-in for the AS implementation, or
  we port `workers-oauth-provider` to a neutral runtime (several
  community ports exist but none battle-tested).

Recommendation: **prototype both**. If the on-device AS takes >2 weeks
or hits security-review blockers, switch to AS-on-Relay for production
and keep the on-device AS as the BYO-tunnel fallback.

## Open Questions

- Do we want per-tool scopes (e.g. `mcp:shots:read`) or is the
  three-level model enough? Start with three; split later if needed.
- Refresh-token lifetime: 30 d vs 90 d. Leaning 30 d with sliding
  renewal.
- Relay hosting: Cloudflare Workers + Durable Objects vs a small Fly
  app. Decided separately; doesn't affect the on-device spec.
