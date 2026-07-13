## ADDED Requirements

### Requirement: Stateful Sessions Are Established Only by an SSE Stream

The MCP server SHALL treat a session as **stateful** only once the client establishes a Server-Sent Events (SSE) stream for that session (a `GET /mcp` that succeeds and is retained for notifications). A session created solely by a `POST` `initialize` handshake, with no subsequent SSE stream, SHALL be treated as **ephemeral** and SHALL NOT retain durable server-side state beyond what is needed to answer the requests on its own connection.

Access-level, confirmation-level, origin, protocol-version, and rate-limit gating SHALL apply identically to ephemeral and stateful sessions; this requirement changes only session retention, not the security surface.

#### Scenario: POST-only client is served without a durable session

- **WHEN** a client sends `initialize` followed by tool calls over `POST` and never opens an SSE stream
- **THEN** the server answers every request successfully
- **AND** the server does not retain a durable session slot for that client once its in-flight requests are complete

#### Scenario: SSE subscriber keeps a durable session

- **WHEN** a client completes `initialize` and then opens an SSE stream via `GET /mcp` for that session
- **THEN** the server retains the session, its negotiated capabilities, and its resource subscriptions for the lifetime of the SSE stream
- **AND** resource-change notifications continue to be pushed to that client

### Requirement: Concurrency Limit Counts Only Stateful Sessions

The `MaxSessions` concurrency limit SHALL count only stateful (SSE-backed) sessions. Ephemeral POST-only sessions SHALL NOT be counted against `MaxSessions`, and their presence SHALL NOT cause a `-32000 "Too many sessions"` rejection of any client.

#### Scenario: Repeated re-initializing client cannot exhaust the pool

- **WHEN** a client re-runs `initialize` on every request without ever echoing the `Mcp-Session-Id` header or opening an SSE stream (the observed ChatGPT `openai-mcp` connector behavior)
- **THEN** the server continues to answer that client's requests
- **AND** no request from any other client is rejected with `-32000 "Too many sessions"` as a result of that churn

#### Scenario: Stateful sessions still bounded

- **WHEN** the number of concurrent SSE-backed sessions reaches `MaxSessions`
- **THEN** a further attempt to establish a new stateful (SSE) session is refused
- **AND** the refusal does not disturb existing stateful sessions or ephemeral request handling

### Requirement: Total Session Pool Is Bounded Against Churn Without Rejecting Clients

Because ephemeral sessions are no longer bounded by the stateful concurrency limit and the `initialize` handshake is not rate-limited, the server SHALL enforce an absolute backstop on the total number of retained sessions to bound memory. When creating a session would exceed that backstop, the server SHALL evict the least-recently-active ephemeral session (never a stateful session, and never one holding a pending in-app confirmation) rather than rejecting the new session. A burst of per-request re-initialization SHALL NOT cause any client's request to be rejected.

#### Scenario: Tight-loop initialize is bounded by eviction, not rejection

- **WHEN** a client POSTs `initialize` in a tight loop without echoing a session header, driving the total session count to the backstop
- **THEN** the total retained session count stays bounded at the backstop
- **AND** each new `initialize` still succeeds, the server having evicted the least-recently-active ephemeral session to make room

#### Scenario: Eviction never targets a stateful or confirming session

- **WHEN** the pool reaches the backstop while some sessions hold a live SSE stream or a pending in-app confirmation
- **THEN** eviction skips those sessions and removes only an ephemeral, non-confirming one

### Requirement: Ephemeral Sessions Are Reaped Well Before the Stateful Timeout

Ephemeral (non-SSE) session state SHALL be released by a reaping pass bounded well below the idle-session timeout used for stateful sessions, so that per-request re-initializing clients do not accumulate durable state up to the full stateful timeout. A session with an in-flight request or a pending in-app confirmation SHALL NOT be reaped.

#### Scenario: Ephemeral state does not linger to the stateful timeout

- **WHEN** an ephemeral session has been idle past the ephemeral-reaping bound but well short of the stateful idle-session timeout
- **THEN** the server has released that session's state

#### Scenario: In-flight or confirming ephemeral session is not reaped

- **WHEN** an ephemeral session has a tool call still executing or a pending in-app confirmation
- **THEN** the server does not release that session's state until the request completes and any confirmation resolves
