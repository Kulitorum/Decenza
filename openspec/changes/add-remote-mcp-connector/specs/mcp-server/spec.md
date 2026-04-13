## ADDED Requirements

### Requirement: Streamable HTTP Protocol Version

The MCP server SHALL advertise `2025-06-18` as its preferred protocol
version and SHALL accept `2025-03-26` as a fallback. Earlier versions
(e.g. `2024-11-05`) SHALL be rejected.

#### Scenario: Client requests current version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-06-18"`
- **THEN** the server responds with `protocolVersion: "2025-06-18"`

#### Scenario: Client requests legacy version
- **WHEN** a client sends `initialize` with `protocolVersion: "2024-11-05"`
- **THEN** the server responds with an error indicating the version is unsupported and lists the supported versions

### Requirement: OAuth 2.1 Protected Resource

When remote MCP is enabled, every `/mcp` request SHALL carry a valid
`Authorization: Bearer <token>` header. Requests without a token, or
with an expired/revoked token, SHALL receive HTTP `401` with a
`WWW-Authenticate: Bearer resource_metadata="<url>"` header pointing
to the protected-resource metadata document.

#### Scenario: Unauthenticated request
- **WHEN** a client POSTs to `/mcp` without an `Authorization` header and remote MCP is enabled
- **THEN** the server returns `401` with `WWW-Authenticate: Bearer resource_metadata="https://<host>/.well-known/oauth-protected-resource"`

#### Scenario: Expired access token
- **WHEN** a client presents an access token whose `exp` claim is in the past
- **THEN** the server returns `401` with `WWW-Authenticate: Bearer error="invalid_token"`

#### Scenario: Valid token with sufficient scope
- **WHEN** a client presents a valid token with scope `mcp:control` and calls a control-level tool
- **THEN** the server executes the tool and returns the result

#### Scenario: Valid token with insufficient scope
- **WHEN** a client presents a valid token with scope `mcp:read` and calls a control-level tool
- **THEN** the server returns a JSON-RPC error with code `-32002` and message indicating insufficient scope

### Requirement: OAuth Authorization Server Metadata

The server SHALL publish RFC 8414 metadata at
`/.well-known/oauth-authorization-server` and RFC 9728 protected-resource
metadata at `/.well-known/oauth-protected-resource`, both reachable
without authentication.

#### Scenario: AS metadata discovery
- **WHEN** a client GETs `/.well-known/oauth-authorization-server`
- **THEN** the response is `200` JSON containing `issuer`, `authorization_endpoint`, `token_endpoint`, `registration_endpoint`, `revocation_endpoint`, `code_challenge_methods_supported: ["S256"]`, and `scopes_supported: ["mcp:read","mcp:control","mcp:full"]`

#### Scenario: Protected-resource metadata discovery
- **WHEN** a client GETs `/.well-known/oauth-protected-resource`
- **THEN** the response is `200` JSON containing `resource` (the MCP endpoint URL) and `authorization_servers` (a list containing the device's AS issuer)

### Requirement: Dynamic Client Registration

The server SHALL accept RFC 7591 client registration at
`/oauth/register` without prior credentials, and SHALL return a unique
`client_id` for each registration.

#### Scenario: New client registers
- **WHEN** a client POSTs `{"client_name":"Claude","redirect_uris":["https://claude.ai/api/mcp/auth_callback"]}` to `/oauth/register`
- **THEN** the server returns `201` with a JSON body containing `client_id`, the echoed `redirect_uris`, and `token_endpoint_auth_method: "none"`

### Requirement: Authorization Code with PKCE

The `/oauth/authorize` endpoint SHALL require PKCE with method `S256`
and SHALL reject requests with a missing or non-`S256`
`code_challenge_method`. The `/oauth/token` endpoint SHALL verify
`code_verifier` against the stored `code_challenge`.

#### Scenario: Authorize without PKCE
- **WHEN** a client GETs `/oauth/authorize` without `code_challenge`
- **THEN** the server redirects to the client's `redirect_uri` with `error=invalid_request`

#### Scenario: Token exchange with wrong verifier
- **WHEN** a client POSTs to `/oauth/token` with a `code_verifier` that does not match the stored challenge
- **THEN** the server returns `400` with `error=invalid_grant`

### Requirement: Federated Identity via Google or Microsoft

Before showing the on-device consent dialog, `/oauth/authorize` SHALL
redirect the user to Google or Microsoft for OIDC sign-in and SHALL
verify the returned `id_token` (`iss`, `aud`, `exp`, signature via the
IdP's JWKS). The authenticated `sub` SHALL match the device owner
configured for that IdP; a mismatch SHALL deny the request.

#### Scenario: First-time device owner setup
- **WHEN** the user enables remote MCP and completes Google or Microsoft sign-in during first-run setup
- **THEN** the device stores the IdP issuer and `sub` as the device owner identity

#### Scenario: Authorized owner signs in
- **WHEN** a connector initiates `/oauth/authorize` and the user signs in with the IdP matching the stored device owner
- **THEN** the flow proceeds to the on-device consent dialog

#### Scenario: Wrong account signs in
- **WHEN** the IdP returns a valid `id_token` whose `sub` does not match the stored device owner
- **THEN** the server redirects to the client's `redirect_uri` with `error=access_denied` and logs the mismatch

#### Scenario: Invalid id_token signature
- **WHEN** the IdP callback returns an `id_token` that fails JWKS signature verification
- **THEN** the server redirects with `error=access_denied` and does not show the consent dialog

### Requirement: On-Device User Consent

The device SHALL display a consent dialog on its own screen after
successful IdP sign-in and before `/oauth/authorize` returns a code,
showing the requesting client's name, its redirect URI host, and the
requested scopes. The authorization request SHALL only complete on
explicit user approval.

#### Scenario: User approves
- **WHEN** the consent dialog is shown and the user taps Allow
- **THEN** the server completes the authorization redirect with a valid `code`

#### Scenario: User denies
- **WHEN** the consent dialog is shown and the user taps Deny
- **THEN** the server redirects to the client's `redirect_uri` with `error=access_denied`

#### Scenario: Consent times out
- **WHEN** the consent dialog has been visible for more than 120 seconds with no response
- **THEN** the server treats the request as denied and redirects with `error=access_denied`

### Requirement: Relay Mode Reachability (Option A)

In relay mode, the Decenza app SHALL maintain an outbound WebSocket
connection to `wss://api.decenza.coffee` and SHALL handle inbound
`mcp_request` messages by dispatching to the in-process MCP server and
returning results via `mcp_response`. The relay's HTTPS endpoint
(`https://api.decenza.coffee/v1/mcp/<device-id>`) SHALL be used as the
OAuth `issuer` and `resource` values.

#### Scenario: Relay connected, MCP request arrives
- **WHEN** a valid MCP JSON-RPC request arrives at the relay HTTP endpoint with a valid Bearer token
- **THEN** the relay forwards it to the device WebSocket, the device processes it, returns `mcp_response`, and the relay returns the result to the HTTP caller within 25 seconds

#### Scenario: Device offline
- **WHEN** a MCP request arrives at the relay but the device has no active WebSocket connection
- **THEN** the relay returns `503 Device Offline`

#### Scenario: Remote MCP disabled
- **WHEN** remote MCP is disabled
- **THEN** the device SHALL close its relay WebSocket and the relay SHALL return `503` for any further requests to that device's endpoint

### Requirement: DuckDNS + UPnP Reachability (Option B)

In DuckDNS mode, the server SHALL expose itself directly using a
user-registered DuckDNS subdomain, a UPnP-mapped port 443, and a
Let's Encrypt certificate via ACME DNS-01. The
`https://<subdomain>.duckdns.org` URL SHALL be used as the OAuth
`issuer` and `resource` values.

#### Scenario: Fully configured
- **WHEN** remote MCP is enabled in DuckDNS mode with valid credentials, active UPnP mapping, and a non-expired cert
- **THEN** the settings UI displays the DuckDNS URL and the HTTPS listener is bound on port 443

#### Scenario: CGNAT detected
- **WHEN** the UPnP-reported external IP does not match the outbound probe IP
- **THEN** the server SHALL refuse to enable remote MCP and the UI SHALL suggest switching to relay mode

#### Scenario: UPnP unavailable
- **WHEN** no IGD device responds to SSDP discovery within 5 seconds
- **THEN** the server SHALL refuse to enable remote MCP and the UI SHALL display instructions to enable UPnP or switch to relay mode

#### Scenario: ACME renewal
- **WHEN** the installed cert is within 30 days of expiry
- **THEN** the server SHALL attempt ACME renewal via DuckDNS DNS-01; on success the new cert SHALL be hot-swapped without dropping connections

#### Scenario: Remote MCP disabled
- **WHEN** remote MCP is disabled
- **THEN** the HTTPS listener SHALL stop, the UPnP mapping SHALL be released, and DDNS updates SHALL cease

### Requirement: Client Revocation

The settings UI SHALL list every registered client with its last-used
timestamp and SHALL provide a revoke action that immediately
invalidates all refresh and access tokens issued to that client.

#### Scenario: User revokes a client
- **WHEN** the user taps Revoke on a listed client
- **THEN** subsequent MCP requests carrying any token previously issued to that client return `401 invalid_token` within one second
