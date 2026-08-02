## Why

An audit of Decenza's MCP server against the normative text of every protocol
revision it negotiates (2024-11-05 through 2025-11-25) found six places where the
wire behaviour departs from the spec — two of them `MUST`s that a conformant
client is entitled to rely on, one field the spec does not define at all, and
three `SHOULD`s.

The audit was prompted by a defect of exactly this shape: every MCP tool failure
was being reported to clients as a successful call, because the failure signal
was lost during response wrapping
([#1754](https://github.com/Kulitorum/Decenza/pull/1754)). That bug was invisible
for the same reason these are — the server is only ever exercised by a handful of
tolerant clients, so a departure from the spec produces no visible symptom until
a stricter client arrives. Fixing them together, from one audit, is cheaper than
rediscovering each from a field report.

## What Changes

- **JSON-RPC batches are accepted.** A POST whose body is a JSON array is
  processed element by element instead of rejected as a parse error. Required of
  every server by the 2024-11-05 and 2025-03-26 base protocols, both of which
  Decenza still negotiates.
- **A terminated session responds HTTP 404.** Session IDs the server has itself
  ended — by `DELETE` or by expiry — are remembered as tombstones and rejected,
  which is what tells a client to re-initialize. Today `DELETE` terminates
  nothing a client can observe.
  - **The existing auto-recovery is retained for IDs the server never issued**
    (a restart, a stale ID from another instance, a garbage value). That path
    exists because cloud connectors re-initialize per request without echoing
    the session header (`2026-07-13-stateless-mcp-session-handling`), and this
    change must not undo it.
- **`structuredContent` is removed from resource content blocks.** It is not a
  field of `ResourceContents` in any MCP revision — it exists only on
  `CallToolResult`. It is currently emitted inside `resources/read` contents and
  version-gated as though it were a 2025-06-18 resources feature, with a test
  pinning that mistaken premise. **BREAKING** for any client reading it, which
  the spec gives none reason to.
- **A missing resource reports `-32002`, not `-32602`.** The resources spec names
  `-32002` for "Resource not found"; every read failure currently collapses to
  Invalid params, and none carries the `data.uri` the spec's example shows.
- **An unknown tool reports `-32602`, not `-32603`.** The tools spec's own
  example returns Invalid params for `Unknown tool: …`; Decenza reports it as an
  Internal error, which describes a server fault rather than a bad request.
- **The SSE stream primes the client for reconnection.** An initial event
  carrying an event ID and empty `data`, per-event IDs thereafter, and a `retry`
  field — the 2025-11-25 `SHOULD`s that let a client resume rather than restart.
  Replay of missed events (`Last-Event-ID`) is a `MAY` and stays out of scope.

Explicit non-goals: the server continues to bind all interfaces rather than
localhost. That is the one audit finding deliberately left as-is — Decenza's MCP
endpoint is served by ShotServer, whose entire purpose is LAN reachability, and
the spec's localhost `SHOULD` is aimed at servers with no such requirement. The
`Origin` allowlist and the capability-URL gate on the remote surface are the
mitigations, and both already exist.

## Capabilities

### Modified Capabilities

- `mcp-server`: adds requirements for batch acceptance, terminated-session
  rejection, resource-content field composition, and the two JSON-RPC error
  codes; adds the SSE reconnection-priming requirements.

## Impact

- `src/mcp/mcpserver.cpp` — the POST body parse (batch), session lookup and
  `DELETE`/expiry (tombstones), `handleResourcesRead` (error code, field
  removal), `handleToolsCall` (error code), and the `GET` SSE writer (priming,
  IDs, `retry`).
- `src/mcp/mcpsession.h` / `mcpserver.h` — tombstone storage, SSE event-ID
  counter.
- `tests/tst_mcpserver_protocol.cpp`, `tests/tst_mcpserver_session.cpp` — the
  existing test asserting `structuredContent` on `resources/read` encodes the
  premise being removed and must go with it.
- `docs/CLAUDE_MD/MCP_SERVER.md` — the resource-response shape and the error-code
  convention.
- **Wire-visible for MCP clients**, all of it. Per the project's standing
  position, MCP surfaces are free to change because clients reload schemas every
  connection; the risk that needs live verification is the 404, which is the one
  change that can make a tolerant client stop working rather than start working.
