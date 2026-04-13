# Change: Upgrade MCP server for Claude & OpenAI custom connectors

## Why
Decenza's MCP server today listens on plain HTTP on the LAN with no
authentication. Claude (claude.ai / Claude mobile) and OpenAI (ChatGPT
custom connectors) can only talk to an MCP endpoint that is:

1. Reachable on the public internet over HTTPS,
2. Speaks the current **Streamable HTTP** transport (protocol
   `2025-06-18`),
3. Authenticates the caller via **OAuth 2.1** with PKCE and
   **Dynamic Client Registration** (RFC 7591), advertised through
   `.well-known/oauth-protected-resource` and
   `.well-known/oauth-authorization-server`.

Meeting those three gates unlocks using Decenza's MCP from the Claude
and ChatGPT **mobile** apps (and desktop/web), which is the whole point
— today you have to run `mcp-remote` on a laptop on the same LAN.

## What Changes

- **BREAKING**: Bump supported MCP protocol version to `2025-06-18`
  (Streamable HTTP). Keep `2025-03-26` as a fallback for existing
  `mcp-remote` users; drop `2024-11-05`.
- Add an **OAuth 2.1 authorization server** inside Decenza that
  **federates identity to Google and Microsoft** via OIDC:
  - Authorization Code + PKCE (S256) only; no implicit, no client_secret flow.
  - Dynamic Client Registration endpoint (`/oauth/register`) per RFC 7591
    so Claude/OpenAI can self-register without the user copy-pasting
    client IDs. (Google/Microsoft don't support open DCR, which is why
    Decenza must be the AS rather than pointing connectors directly at
    them.)
  - Before issuing a code, the user signs in with Google or Microsoft.
    Decenza verifies the OIDC `id_token` and checks the `sub`/`email`
    against the device owner configured in Settings. A mismatch is a
    hard deny.
  - Token endpoint issuing short-lived (1 h) access tokens + refresh
    tokens, scoped to the configured MCP access level.
  - Consent screen rendered in the QML app on the device after
    successful IdP sign-in ("Claude is requesting access to your
    espresso machine — Read / Control / Full").
  - Metadata documents at `/.well-known/oauth-protected-resource` and
    `/.well-known/oauth-authorization-server`.
- First-run setup for remote MCP: user picks Google or Microsoft, signs
  in once, and that identity becomes the device owner. Identity is
  required — there is no device-only passcode fallback in this change
  (tracked as a follow-up if users request it).
- Require `Authorization: Bearer <token>` on every MCP request once
  remote mode is enabled; return `401` with `WWW-Authenticate: Bearer
  resource_metadata="..."` per MCP auth spec so clients discover the
  AS.
- Add a **public-reachability** story. The connector URL must be
  reachable from Anthropic/OpenAI's cloud backends — LAN-only and
  tailnet-only options don't work (Tailscale Funnel is also ruled out
  because it doesn't support Android). Two options are offered in
  Settings → Remote MCP:

  **Option A — Decenza relay (recommended, works everywhere)**
  Extend the existing `api.decenza.coffee` infrastructure
  (AWS Lambda + API Gateway WebSocket, already running for Pocket)
  with an MCP proxy endpoint. The device keeps its existing outbound
  WebSocket open; Claude/OpenAI hit
  `https://api.decenza.coffee/v1/mcp/<device-id>`. No port-forwarding,
  no CGNAT issue, no extra software required.
  - New Lambda `mcpProxy.ts`: validates Bearer JWT, looks up the
    device's WebSocket connection ID in DynamoDB, forwards the MCP
    JSON-RPC with a correlation ID, polls DynamoDB for the response
    (up to 25 s), returns it as HTTP JSON.
  - New WebSocket message actions `mcp_request` / `mcp_response` in
    the existing `wsMessage.ts`.
  - OAuth AS Lambdas (`oauthAuthorize`, `oauthToken`, `oauthRegister`,
    `oauthRevoke`) added to the same stack; device generates a
    per-device Ed25519 keypair and publishes its public key to the
    relay's JWKS endpoint at setup time.
  - Infrastructure cost: pennies/month on an already-running stack.
    If this becomes a premium feature later, a subscription can be
    added without changing the architecture.

  **Option B — DuckDNS + UPnP + Let's Encrypt (no relay dependency)**
  For users who prefer no third-party relay. The device handles
  everything locally:
  1. User registers a DuckDNS subdomain + token.
  2. Decenza keeps the A/AAAA record current on network change.
  3. Decenza opens port 443 via UPnP (fallback NAT-PMP) and
     detects CGNAT — if behind CGNAT, refuses and suggests Option A.
  4. Decenza obtains a Let's Encrypt cert via ACME DNS-01 and renews
     30 days before expiry.
  5. The OAuth AS and HTTPS listener run entirely on-device at
     `https://<subdomain>.duckdns.org`.
- Settings UI:
  - Toggle "Enable remote MCP (Claude / ChatGPT)".
  - Shows the public URL to paste into the connector setup.
  - Shows a QR code for mobile connector setup.
  - Lists authorized clients with last-used timestamp and a revoke
    button.
- Scope model: map OAuth scopes to existing MCP access levels
  (`mcp:read`, `mcp:control`, `mcp:full`) so the consent screen can show
  meaningful choices instead of all-or-nothing.

## Impact

- **Affected specs**: `mcp-server` (new capability — no spec exists yet;
  this proposal introduces the first formal spec for the MCP surface,
  limited to remote-connector concerns).
- **Affected code**:
  - `src/mcp/mcpserver.{h,cpp}` — transport upgrade, auth middleware,
    protocol version bump.
  - `src/mcp/` — new `mcpoauth.{h,cpp}` (on-device AS, used by Option
    B and for keypair management in Option A), `mcpidpclient.{h,cpp}`
    (Google/Microsoft OIDC), `mcprelayclient.{h,cpp}` (Option A:
    outbound WebSocket to `api.decenza.coffee`), `mcpduckdns.{h,cpp}`
    (Option B: DDNS + ACME DNS-01), `mcpupnp.{h,cpp}` (Option B: UPnP
    IGD + CGNAT detection).
  - `src/core/settings.{h,cpp}` — remote-MCP toggle, mode (relay /
    duckdns), DuckDNS credentials, current public URL, cert + keys,
    registered clients, refresh tokens (QtKeychain / encrypted
    QSettings).
  - `qml/pages/SettingsPage.qml` + new `RemoteMcpTab.qml` and
    `McpConsentDialog.qml`.
  - **decenza-shotmap repo**: new Lambdas (`mcpProxy.ts`,
    `oauthAuthorize.ts`, `oauthToken.ts`, `oauthRegister.ts`,
    `oauthRevoke.ts`), new DynamoDB tables (oauth tokens, registered
    clients), Terraform updates.
- **Security**: first time the app accepts unauthenticated inbound
  connections from the internet. All new endpoints must be covered by
  the OAuth gate; the pre-existing localhost HTTP listener stays
  unauthenticated but is bound to loopback only when remote mode is
  on.
- **User impact**: existing `mcp-remote` users keep working (LAN + no
  auth stays the default). Remote mode is opt-in.
