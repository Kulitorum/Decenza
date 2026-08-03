## Context

`McpServer` serves the MCP endpoint over Streamable HTTP from inside ShotServer.
It negotiates four protocol revisions (2024-11-05, 2025-03-26, 2025-06-18,
2025-11-25) and already gets the hard parts right: `Origin` validation with a
403, version negotiation and the `MCP-Protocol-Version` header check, 405 on a
non-SSE `GET`, 202 on notifications, and capability declarations that match what
it actually emits (no `listChanged` claimed, none sent).

The six gaps here are what a line-by-line read against the normative text turned
up. They share a cause: the server is exercised by a small set of tolerant
clients — `mcp-remote`, Claude Desktop, the ChatGPT and claude.ai connectors — so
a departure from the spec produces no symptom until a stricter client arrives.
That is the same reason tool failures shipped as successes for the life of the
feature.

Two constraints shape the work:

- **The session lifecycle is deliberately lenient.** `2026-07-13-stateless-mcp-session-handling`
  established that cloud connectors re-`initialize` per request and do not echo
  `Mcp-Session-Id`, so the server auto-recovers unknown session IDs rather than
  rejecting them. A naive "404 anything we don't recognize" would re-break
  exactly what that change fixed.
- **`_deferred` responses leave the normal return path.** In-app confirmation and
  async tools return a `{_deferred: true}` sentinel and answer later from a
  different call site. Anything that assembles multiple responses into one HTTP
  body has to reckon with that.

## Goals / Non-Goals

**Goals:**

- Close the two `MUST` gaps (batch receiving, terminated-session 404) without
  regressing the connector interop the stateless-session change bought.
- Stop emitting a field MCP does not define, and delete the test that pins the
  belief that it is a real field.
- Report the error codes the spec names, so a client can distinguish "no such
  resource" from "bad arguments" and "no such tool" from "server broke".
- Give SSE clients what they need to resume a dropped connection.

**Non-Goals:**

- **SSE replay.** Honouring `Last-Event-ID` to redeliver missed events is a
  `MAY`, and it requires per-stream buffering with an eviction policy. Emitting
  IDs without replaying is explicitly permitted and is where this stops.
- **Binding to localhost.** Covered in the proposal: ShotServer exists to be
  reachable on the LAN.
- **Auditing which failures *should* be errors.** Unchanged from #1754: this
  changes codes and shapes, never which conditions are failures.
- **Sending batches.** `MAY`, and nothing here needs it.

## Decisions

### Batches are accepted at every negotiated version, not gated to the legacy ones

Batching is required of servers by 2024-11-05 and 2025-03-26 and was removed in
2025-06-18, where clients `MUST NOT` send them. A version gate would therefore
add a branch whose only job is to reject a message no conformant client sends.
Accepting an array regardless is the smaller implementation and cannot surprise
a client that never sends one.

*Alternative considered:* gate on `session->protocolVersion() < "2025-06-18"`.
Rejected — more code, no behaviour difference in practice, and it would fail a
2025-06-18 client that batches by mistake instead of just answering it.

### A batch element that would defer is answered with an error, not deferred

An element returning `{_deferred: true}` cannot be folded into the array: its
real response arrives later, from `confirmationResolved` or
`sendAsyncToolResponse`, which write a complete HTTP response to the socket. If
one arrived mid-batch the client would get a JSON-RPC response outside the array
it is waiting on, and then a second body on a socket already answered.

So a batched element that needs confirmation or async dispatch gets a JSON-RPC
error in its array slot, naming the reason. This is a real limitation and is
documented rather than hidden — it is also close to unreachable: batching is a
legacy-client feature and the deferring tools are `machine_start_*` plus the
async families, which no batching client has ever called here.

*Alternative considered:* hold the batch open until every deferred element
resolves. Rejected — that is an aggregation state machine with its own timeout
and socket-lifetime handling, built for a case with no known caller, and the
`_deferred` path already has no watchdog of its own.

### Termination is remembered; absence of knowledge is not termination

The spec's `MUST` is scoped to sessions the server *terminated*: "The server
**MAY** terminate the session at any time, after which it **MUST** respond to
requests containing that session ID with HTTP 404 Not Found." It says nothing
about IDs the server never issued.

That distinction is exactly what makes this safe to implement. The server
records the IDs it ends — `DELETE`, and the expiry reaper — in a bounded
tombstone set, and 404s those. An ID that is merely *unrecognized* keeps the
existing auto-recovery, because the server cannot know whether it issued it
before a restart, and that path is load-bearing for the cloud connectors.

The tombstone set is capped and evicted oldest-first. A session ID is a UUID, so
the memory is trivial, but an unbounded set on a long-running server is a leak
with a slow fuse. On overflow the oldest tombstone is dropped and that ID
degrades to the auto-recovery path — the pre-existing behaviour, not a new
failure mode.

*Alternative considered:* 404 every unrecognized ID (full conformance).
Rejected — it re-breaks the connector interop the stateless-session change was
written to fix, and the spec does not ask for it.

*Alternative considered:* tombstone only explicit `DELETE`s, not expiry.
Rejected as the default because expiry *is* termination and it is the common
case; kept as the documented fallback if live testing shows `mcp-remote` cannot
recover from a 404 (see Risks).

### `structuredContent` leaves resource contents entirely

In the 2025-11-25 schema, `ResourceContents` is `uri` + `mimeType` + `_meta`,
extended by `TextResourceContents` (`text`) and `BlobResourceContents` (`blob`).
`structuredContent` appears on `CallToolResult` and nowhere else. Emitting it
inside a `contents[]` entry is not a version-gated feature, it is a field the
schema does not have — so there is nothing to gate, and the version check around
it is as wrong as the field.

The `text` block already carries the same JSON, so no information is lost. The
test asserting its absence at 2024-11-05 goes with it: it does not merely
over-specify, it asserts the wrong thing is right, and leaving it would make the
removal look like a regression.

*Alternative considered:* move it to `_meta`, which is the schema's extension
point. Rejected — no client reads it, so this would preserve a payload nobody
consumes at the cost of a nonstandard convention to explain.

### Error codes: only the two the spec names

`-32002` for a missing resource and `-32602` for an unknown tool are both
spelled out in the spec (the resources page names the code; the tools page shows
it in an example). Those two change.

The two neighbouring registry failures — "Tool is async" reaching the
synchronous path, and "Access level insufficient" — stay `-32603`. The first is
an internal inconsistency, which is what Internal error means. The second has no
code assigned by MCP, and inventing one, or reusing Invalid params for an
authorization outcome, would say something the spec does not.

### SSE: prime and pace, do not replay

On stream open, send one event with an ID and empty `data` (the priming event
the spec asks for), plus a `retry` field so a reconnecting client waits a sane
interval. Subsequent events carry IDs from a per-session counter, which is what
the spec requires of IDs when they are present: unique across all streams within
the session.

The server does not act on `Last-Event-ID`. That is permitted, and it is the
honest position: the alternative is a replay buffer whose absence today costs
nothing, since the only pushed message is a resource-updated notification the
client can resolve by re-reading the resource.

## Risks / Trade-offs

- **A 404 could wedge a client that cannot re-initialize** → The in-code comment
  on the auto-recovery path claims `mcp-remote` "can't re-initialize on its own",
  which is precisely the client most likely to hold a session long enough to
  meet the expiry reaper. Per the spec a client receiving 404 `MUST` start a new
  session, so a client that cannot is non-conformant — but that is no comfort to
  a user whose LAN setup stops working. Mitigation: verify live against
  `mcp-remote`, Claude Desktop and both cloud connectors before merge, exercising
  an expired session and not just a `DELETE`. If `mcp-remote` wedges, fall back
  to tombstoning `DELETE` only and state the residual gap in the spec delta.
- **Batch handling touches the single-message path** → The parse site is shared
  by every request. Mitigation: the array branch is added around the existing
  object branch, which stays byte-identical, and the existing protocol tests
  cover the object path.
- **Removing `structuredContent` is breaking for anything reading it** → Nothing
  in this repo does, and no MCP client can be reading a field the schema does not
  define. Mitigation: grep the tree and the ShotServer JS before removing;
  mention it in the PR body as a wire change.
- **New SSE fields could confuse a client that mis-parses them** → `id` and
  `retry` are standard `text/event-stream` fields that every SSE parser handles;
  a client ignoring them is unaffected.

## Migration Plan

None. No schema, no persisted state, no settings. Every change is to what the
server writes on the wire, and clients renegotiate on each connection.

Rollback is per-item: the six are independent and can be reverted individually
if one turns out to break a client, which is the main reason to keep them as
separate commits.

## Open Questions

- **Does `mcp-remote` actually fail to recover from a 404?** The claim comes from
  a code comment, not a cited observation, and the whole tombstone design is
  hedged around it. Worth establishing by experiment during implementation
  rather than inheriting.
- **What `retry` interval?** 3000 ms is a common default; nothing here depends on
  the value, and the keepalive probe already runs on a 30 s tick, so the two
  should be picked to not fight each other.
