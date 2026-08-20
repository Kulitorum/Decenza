## MODIFIED Requirements

### Requirement: MCP-Protocol-Version Request Header

For every HTTP request other than `initialize`, the server SHALL accept the
`MCP-Protocol-Version` request header.

A request naming a version the server does not support SHALL be rejected with
HTTP 400. A request naming a version the server DOES support SHALL be served,
whether or not it matches the version negotiated for the session, and SHALL be
answered under the version the header names.

This narrows a previous requirement that rejected any header differing from the
negotiated version. The protocol licenses 400 for an "invalid or unsupported"
version only, and makes matching the negotiated one a client-side SHOULD; the
old rule refused versions the server plainly serves, which the official
conformance suite fails on.

Honouring the header SHALL NOT alter the session's negotiated version: it
answers one request, it does not re-version a live session.

When the header is absent, the server SHALL assume `2025-03-26` per the
protocol's compatibility rule.

#### Scenario: Header matches negotiated version

- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2025-11-25` after negotiating `2025-11-25`
- **THEN** the server processes the request normally

#### Scenario: Header mismatch

- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25`
- **THEN** the request is served, and the response carries only the fields the header's version defines

Note the outcome reverses the previous version of this scenario, which required
HTTP 400 here. The name is kept so the change is visible as a reversal rather
than as one scenario disappearing and an unrelated one appearing.

#### Scenario: Header names an unsupported version

- **WHEN** a client POSTs `tools/call` with an `MCP-Protocol-Version` the server does not support
- **THEN** the server returns HTTP 400 naming the unsupported version

#### Scenario: A supported header does not re-version the session

- **WHEN** a client sends one request under a supported header differing from its negotiated version, then a further request with no header
- **THEN** the second request is answered under the originally negotiated version

#### Scenario: Header absent on legacy client

- **WHEN** a client negotiates `2025-03-26` and POSTs subsequent requests without an `MCP-Protocol-Version` header
- **THEN** the server processes the request normally, assuming `2025-03-26`

## ADDED Requirements

### Requirement: The Server Conforms To Every Protocol Revision It Advertises

Advertising a protocol revision is a claim to implement it. The server SHALL
conform to every revision it advertises, legacy revisions included.

Where the protocol's own conformance suite covers a revision, conformance SHALL
be verified against it rather than against this project's tests alone. Where it
does not — the suite does not cover every revision this server advertises — a
green run SHALL NOT be reported as evidence about the revisions it did not
exercise.

Where the server deviates from a revision it advertises, the deviation SHALL be
deliberate, SHALL be recorded at the point in the code where it occurs, and
SHALL state what it protects. A deviation that exists to keep a real client
working is permitted; an unexamined one is not.

A revision the server cannot conform to SHALL NOT be advertised.

#### Scenario: An advertised revision is exercised

- **WHEN** the conformance suite is run against the server for a revision the suite covers
- **THEN** every requirement of that revision either passes, or fails at a point the code documents as a deliberate deviation

#### Scenario: An advertised revision the suite does not cover

- **WHEN** the server advertises a revision the conformance suite has no scenarios for
- **THEN** its conformance is reported as unverified rather than implied by the other revisions' results

#### Scenario: A deviation protects a client the spec would break

- **WHEN** conformance requires behaviour that would leave a known real client unable to recover
- **THEN** the deviation is kept, and the code records which client it protects and why

#### Scenario: A revision that cannot be served

- **WHEN** the server cannot conform to a revision
- **THEN** that revision is absent from the list of versions the server advertises

### Requirement: Session Requirements Govern The Legacy Era Only

Every requirement in this capability concerning sessions — how a session
becomes stateful, the concurrency limit, the total pool bound, the reaping of
ephemeral sessions, and the rejection of a terminated session — SHALL be read
as governing the legacy era, in which sessions exist.

The modern era has no sessions for those requirements to govern. Their absence
there is not a gap in conformance.

#### Scenario: A modern request and the session limits

- **WHEN** the server is serving modern requests
- **THEN** no session is created for them, and they are not counted against any session limit

#### Scenario: Legacy sessions are still bounded

- **WHEN** legacy clients connect
- **THEN** every session limit in this capability applies to them exactly as before

### Requirement: List Results Are Returned In A Deterministic Order

`tools/list` and `resources/list` SHALL each return their entries in an order
that is stable across process restarts for an unchanged set of entries, so that
a client may cache the result and so that a repeated listing does not defeat
prompt caching.

The order SHALL NOT depend on the iteration order of an unordered container.

#### Scenario: Two runs return the same order

- **WHEN** a client lists tools, the server restarts with the same tools registered, and the client lists tools again
- **THEN** the two responses carry the tools in the same order

#### Scenario: Two runs return the same resource order

- **WHEN** a client lists resources, the server restarts with the same resources registered, and the client lists resources again
- **THEN** the two responses carry the resources in the same order

#### Scenario: Order is independent of registration order

- **WHEN** the order in which tools are registered changes but the set of tools does not
- **THEN** the listing order is unchanged

### Requirement: List And Read Results Carry Cache Guidance

`tools/list`, `resources/list` and `resources/read` SHALL carry a freshness
hint stating how long the result may be reused, and a scope stating whether
the result may be cached beyond the requesting caller.

In the modern era these SHALL be present on every such result; they are not
optional there.

A result whose content depends on the caller's access level SHALL NOT be
marked cacheable beyond that caller.

#### Scenario: A client caches a tool listing

- **WHEN** a client lists tools
- **THEN** the response states how long the listing may be reused

#### Scenario: Access-dependent results are not shared

- **WHEN** a listing reflects the caller's access level
- **THEN** its cache scope does not permit reuse for another caller
