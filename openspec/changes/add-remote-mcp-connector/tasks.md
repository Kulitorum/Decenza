## 1. Transport upgrade

- [ ] 1.1 Bump supported MCP protocol versions in `McpServer::handleInitialize` to `{"2025-06-18", "2025-03-26"}`; drop `2024-11-05`.
- [ ] 1.2 Verify Streamable HTTP behavior against `2025-06-18` spec (single `/mcp` endpoint, `Mcp-Session-Id` header, JSON + SSE content negotiation).
- [ ] 1.3 Bind the existing plain-HTTP listener to loopback only when remote mode is enabled.

## 2. OAuth 2.1 authorization server

- [ ] 2.1 Add `src/mcp/mcpoauth.{h,cpp}` implementing `/oauth/authorize`, `/oauth/token`, `/oauth/register`, `/oauth/revoke`.
- [ ] 2.2 Enforce PKCE S256; reject missing/invalid `code_verifier`.
- [ ] 2.3 Issue JWT access tokens (1 h) + opaque refresh tokens; sign with per-device key stored in QtKeychain.
- [ ] 2.4 Serve `/.well-known/oauth-protected-resource` and `/.well-known/oauth-authorization-server`.
- [ ] 2.5 Add `Authorization: Bearer` middleware in `McpServer::handleRequest`; unauthenticated requests return `401` with `WWW-Authenticate: Bearer resource_metadata="<url>"`.
- [ ] 2.6 Map scopes → access levels (`mcp:read`→0, `mcp:control`→1, `mcp:full`→2); enforce at tool-dispatch time.

## 2a. Identity federation (Google / Microsoft OIDC)

- [ ] 2a.1 Register Decenza OAuth apps in Google Cloud Console and Microsoft Entra; ship `client_id`s in build config.
- [ ] 2a.2 Add `src/mcp/mcpidpclient.{h,cpp}`: OIDC discovery, JWKS fetch + caching, PKCE auth-code flow against the IdP, `id_token` verification (`iss`, `aud`, `exp`, signature).
- [ ] 2a.3 `/oauth/authorize` redirects to the IdP first; on callback, verifies `id_token` before showing the device consent dialog.
- [ ] 2a.4 First-run setup captures device owner `sub` + `email` per IdP; store in `Settings`.
- [ ] 2a.5 Reject authorization if the signed-in `sub` doesn't match the stored device owner for that IdP; return `error=access_denied` with a log line naming the mismatch.
- [ ] 2a.6 Discard IdP access/refresh tokens after `id_token` verification — Decenza does not persist IdP tokens.

## 3. Consent UI

- [ ] 3.1 New `qml/components/McpConsentDialog.qml` showing client name, redirect URI host, requested scopes.
- [ ] 3.2 Signal from `McpOAuth` → `MainController` → QML; resolve with allow/deny back to the pending `/oauth/authorize` request.
- [ ] 3.3 Auto-dismiss after 2 min with implicit deny.

## 4a. Option A — Decenza relay (decenza-shotmap repo)

- [ ] 4a.1 **MCP proxy Lambda** (`backend/lambdas/mcpProxy.ts`):
  - Validate `Authorization: Bearer` JWT against the requesting device's stored public key.
  - Look up active WebSocket `connection_id` for the device in DynamoDB.
  - If device not connected, return `503 Device Offline`.
  - Write a pending `mcp_request` record to DynamoDB (`MCP_PENDING#<correlation_id>`), call `sendToConnection` to forward the MCP JSON-RPC body.
  - Poll DynamoDB for `mcp_response` record every 200 ms, up to 25 s (API Gateway HTTP has 29 s timeout). Return 504 on timeout.
  - Delete pending + response records after use.
- [ ] 4a.2 **WebSocket message actions** in `wsMessage.ts`:
  - `mcp_request`: relay-to-device; device receives full MCP JSON-RPC body + `correlation_id`.
  - `mcp_response`: device-to-relay; handler writes `mcp_response` record to DynamoDB keyed by `correlation_id`.
- [ ] 4a.3 **OAuth AS Lambdas**: `oauthAuthorize.ts`, `oauthToken.ts`, `oauthRegister.ts`, `oauthRevoke.ts`. Store clients + tokens in new DynamoDB table. Validate tokens using per-device public key (published by device at registration).
- [ ] 4a.4 **Device keypair registration**: on first relay-mode enable, device generates Ed25519 keypair, calls `POST /v1/mcp/register-device` with `device_id` + public key + pairing token (re-uses existing pairing auth). Relay stores public key in DynamoDB.
- [ ] 4a.5 **Terraform**: new DynamoDB tables (`mcp_pending`, `oauth_clients`, `oauth_tokens`), new Lambda + HTTP API Gateway routes (`/v1/mcp/{device_id}`, `/v1/oauth/*`, `/.well-known/oauth-*`).
- [ ] 4a.6 **Decenza app relay client** (`src/mcp/mcprelayclient.{h,cpp}`): maintain outbound WSS to `wss://api.decenza.coffee` in relay mode; handle `mcp_request` messages by dispatching to `McpServer` in-process and writing the response back via `mcp_response`.

## 4b. Option B — DuckDNS + UPnP + Let's Encrypt (on-device, no relay)

- [ ] 4b.1 DuckDNS setup wizard: prompt for subdomain + API token; verify by calling `https://www.duckdns.org/update`.
- [ ] 4b.2 Periodic IP refresh (hourly + on network change) posting to DuckDNS with backoff on failure.
- [ ] 4b.3 UPnP IGD port mapping for external 443 → internal 443. Minimal SSDP + IGD:1/IGD:2 SOAP client (~200–300 LOC). Fall back to NAT-PMP.
- [ ] 4b.4 CGNAT detection: compare UPnP `GetExternalIPAddress` against an outbound probe. On mismatch, disable and suggest Option A.
- [ ] 4b.5 ACME v2 DNS-01 client (DuckDNS): account key generation, order → TXT challenge → finalize, cert download, renewal 30 days before expiry.
- [ ] 4b.6 HTTPS listener on 443 using issued cert; bind `0.0.0.0` only after CGNAT check passes.
- [ ] 4b.7 Release UPnP mapping cleanly on shutdown / disable.

## 5. Settings UI

- [ ] 5.1 New `qml/pages/settings/RemoteMcpTab.qml` with enable toggle, mode selector (relay | tunnel), public URL display, QR code.
- [ ] 5.2 Authorized clients list with last-used timestamp and revoke button (revokes refresh token + all access tokens for that client).
- [ ] 5.3 i18n keys under `settings.remoteMcp.*`.
- [ ] 5.4 Accessibility review (consent dialog, toggle, list delegates per CLAUDE.md rules).

## 6. Persistence

- [ ] 6.1 Extend `Settings` with `remoteMcpEnabled`, `remoteMcpMode`, `remoteMcpPublicUrl`, `remoteMcpDeviceId`.
- [ ] 6.2 Store registered clients + refresh tokens in QtKeychain when available; fall back to `QSettings` with AES-GCM using a device key.

## 7. Tests

- [ ] 7.1 Unit tests for OAuth flow (PKCE happy path, bad verifier, expired code, refresh).
- [ ] 7.2 Unit tests for scope → access-level mapping.
- [ ] 7.3 Integration test: full `initialize` → `tools/list` over Streamable HTTP with Bearer auth using a test AS.
- [ ] 7.4 Manual QA: connect from Claude mobile and ChatGPT mobile custom connector; verify consent, tool call, revoke.

## 8. Documentation

- [ ] 8.1 Update `docs/CLAUDE_MD/MCP_SERVER.md` with remote-connector setup steps (both relay and BYO tunnel).
- [ ] 8.2 Add screenshots of consent dialog and settings tab.
- [ ] 8.3 Note security model: what scopes grant, where tokens live, how to revoke.
