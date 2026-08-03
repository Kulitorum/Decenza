## ADDED Requirements

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

`tools/list` SHALL return its entries in an order that is stable across
process restarts for an unchanged set of tools, so that a client may cache the
result and so that a repeated listing does not defeat prompt caching.

The order SHALL NOT depend on the iteration order of an unordered container.

#### Scenario: Two runs return the same order

- **WHEN** a client lists tools, the server restarts with the same tools registered, and the client lists tools again
- **THEN** the two responses carry the tools in the same order

#### Scenario: Order is independent of registration order

- **WHEN** the order in which tools are registered changes but the set of tools does not
- **THEN** the listing order is unchanged

### Requirement: List And Read Results Carry Cache Guidance

`tools/list`, `resources/list` and `resources/read` SHALL carry a freshness
hint stating how long the result may be reused, and a scope stating whether
the result may be cached beyond the requesting caller.

A result whose content depends on the caller's access level SHALL NOT be
marked cacheable beyond that caller.

#### Scenario: A client caches a tool listing

- **WHEN** a client lists tools
- **THEN** the response states how long the listing may be reused

#### Scenario: Access-dependent results are not shared

- **WHEN** a listing reflects the caller's access level
- **THEN** its cache scope does not permit reuse for another caller
