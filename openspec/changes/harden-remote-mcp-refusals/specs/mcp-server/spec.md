## MODIFIED Requirements

### Requirement: Capability-URL Authorization for Remote Access

When remote MCP is enabled, the remote surface SHALL authorize
requests solely by an unguessable capability token carried as a URL
path segment (`/mcp/<token>`), where the token is a 128-bit
cryptographically random value generated on-device. Token comparison
SHALL be constant-time.

A request that does not carry the current token — wrong token, missing
token, or framing malformed enough that no token is ever read — SHALL
be refused without revealing that an MCP server exists. Behind an
embedded tunnel the refusal SHALL be no response at all, with the
connection closed; on a bring-your-own-proxy listener it SHALL be a
bare HTTP `404`, because there the response goes to the user's own
reverse proxy, where a silent drop reads as a broken backend.

Silence here SHALL NOT be described as making the endpoint invisible.
The tunnel edge terminates TLS and serves its own error for a backend
that hangs up, so the hostname remains visibly configured; what the
silence withholds is any confirmation from this application.

A request that DOES carry the current token SHALL receive the bare
`404` in either mode when its method or path is not served — its
caller has already proved it knows the token, so silence would buy
nothing but a confused client.

#### Scenario: Valid token
- **WHEN** a client POSTs a JSON-RPC request to `/mcp/<token>` with the current token
- **THEN** the request is dispatched to the MCP server and handled normally

#### Scenario: Wrong token
- **WHEN** a client POSTs to `/mcp/<other>` where `<other>` is not the current token
- **THEN** the request is refused without any MCP-identifying headers or body — closed unanswered behind a tunnel, `404` otherwise

#### Scenario: Missing token
- **WHEN** a client POSTs to `/mcp` on the remote surface
- **THEN** the request is refused the same way — closed unanswered behind a tunnel, `404` otherwise

#### Scenario: Wrong token behind a tunnel
- **WHEN** a client POSTs to `/mcp/<other>` on a tunnel-proxied listener, where `<other>` is not the current token
- **THEN** the connection is closed with no bytes written

#### Scenario: Wrong token on a bring-your-own-proxy listener
- **WHEN** a client POSTs to `/mcp/<other>` on a listener fronted by the user's own reverse proxy
- **THEN** the server returns `404` with no MCP-identifying headers or body

#### Scenario: Malformed framing behind a tunnel
- **WHEN** a request arrives with headers that never terminate, a body over the cap, or a non-numeric `Content-Length`
- **THEN** the refusal is the same as for a wrong token — no reply behind a tunnel, `404` otherwise — since no token was ever read

#### Scenario: Valid token, unserved method
- **WHEN** a client sends a method other than `POST`, `GET` or `DELETE` to `/mcp/<token>` with the current token
- **THEN** the server returns `404` in either mode

## ADDED Requirements

### Requirement: A Remote Caller Is Named By Something A Proxy Cannot Collapse

Where the remote listener is one an embedded tunnel proxies into, every
client reaches it from loopback and the peer address identifies nobody.
Log lines and rate-limiter keys for such a caller SHALL use a label that
says the request came from the public internet, never the proxied
loopback address. That label SHALL be produced once, by the listener
that knows its own exposure, and supplied to any other component that
keys or reports on the caller — a second derivation is free to drift
from the first, and the two would then disagree about who was refused.

A listener that is NOT tunnel-proxied SHALL keep the peer address,
where it is a genuine peer.

#### Scenario: Rejection behind a tunnel
- **WHEN** an unauthorized request arrives on a tunnel-proxied listener
- **THEN** the log line names it as coming from the public internet rather than from `127.0.0.1`

#### Scenario: Rate-limit refusal behind a tunnel
- **WHEN** a tunnel-proxied caller exceeds the stateless era's control-call budget
- **THEN** the refusal line names the same caller the connector logs, not the loopback address

### Requirement: An Unauthenticated Caller Cannot Fill The Debug Log

The debug log is a fixed-size buffer shared by every subsystem, and the
remote surface is reachable by anyone who finds the public URL. The
number of log lines a caller that fails authorization can cause SHALL
be bounded well below one per request.

The per-source failed-token budget SHALL be small enough to reflect
that nothing is learned from repetition: a client holding a valid token
never fails the check, and a person who pasted a truncated URL retries
once or twice. Beyond the budget, the connection SHALL be dropped
rather than answered.

Bounding SHALL NOT mean going silent about scale. After the
per-request lines stop, the system SHALL still record the running
count at increasing intervals, so a submitted log distinguishes a
single stray probe from sustained hammering.

#### Scenario: Sustained rejection
- **WHEN** one source sends many more unauthorized requests than the budget within a minute
- **THEN** the number of warnings emitted is fewer than the number of requests

#### Scenario: Scale is still recorded
- **WHEN** unauthorized requests from one source continue past the point where per-request logging stops
- **THEN** the log still receives lines carrying the running count for that minute
