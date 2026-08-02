## Context

Two functions decide what leaves the MCP server:

- `McpServer::buildToolCallResponse` (`src/mcp/mcpserver.cpp:303`) takes a tool's
  result object and wraps it into `{content: [...], structuredContent: {...}}`,
  gating `structuredContent` and `resource_link` blocks on the negotiated
  protocol version.
- `McpServer::sendJsonRpcResponse` (`:1340`) picks between a JSON-RPC `error` and
  a JSON-RPC `result` with `if (result.contains("error"))`.

For a **plain JSON-RPC method** those compose correctly — nothing wraps the
payload, so a top-level `error` is seen.

For a **tool call** they do not. The payload is wrapped first, so by the time
`sendJsonRpcResponse` runs, `result` is the envelope and the tool's `error` key
is one level down inside it. `contains("error")` is false, the response ships as
`result`, and no `isError` is set anywhere.

Tools report failure by returning `{"error": "..."}` — ~283 sites across
`src/mcp/`. Every one of them is currently reported to the client as a
successful call.

The single exception is the confirmation-denial path (`:1189`), which builds the
envelope and then sets `result["isError"] = true` by hand. That is the correct
shape, applied once, by hand, in the one place a human happened to think about
it.

## Goals / Non-Goals

**Goals:**

- A failed tool call is identifiable from the protocol alone.
- Fixed centrally: no tool file changes, and a new tool inherits the behaviour.
- The error text stays where a model can read it.

**Non-Goals:**

- Changing any tool's error text, adding error paths, or renaming `error`.
- Turning tool failures into JSON-RPC errors (see the decision below).
- Structured error codes/categories. Worth considering later; not this change.
- Auditing which of those sites *should* be errors. Several are arguably
  ordinary empty results. That is a separate judgement call per tool and would
  bury this fix.

## Decisions

### `isError: true`, not a JSON-RPC error

MCP models a tool that ran and failed as a **successful JSON-RPC exchange
carrying a failed tool result** — `result` with `isError: true` — and reserves
JSON-RPC `error` for protocol faults (unknown method, malformed request,
rejected session). A tool reporting "database unavailable" is not a protocol
fault; the call was well-formed and the server answered it.

Converting tool failures to JSON-RPC errors would also destroy the error text
for models, because `content` would not be delivered at all.

*Alternative considered:* have `sendJsonRpcResponse` reach into the envelope and
unwrap. Rejected — it would emit a JSON-RPC error (wrong per above), and it
puts knowledge of the envelope's shape in the transport layer.

### Mark at the wrap site

`buildToolCallResponse` already receives the raw tool payload, before wrapping.
It is the only place that sees both the tool's `error` key and the envelope it is
about to become, so it is the only place that can transfer the signal from one to
the other without re-parsing.

*Consequence:* the denial path's hand-rolled assignment becomes redundant and is
removed, leaving one definition of what a failed tool call looks like — the
"centralize anything produced at more than one site" rule, applied before the
second site multiplies further.

### Leave `sendJsonRpcResponse`'s check alone, and say why

The `contains("error")` branch is **correct for non-tool methods** and must stay.
It gains a comment explaining that it cannot fire for tool calls because the
payload is wrapped first — otherwise the next reader sees a branch that appears
dead for tools and "fixes" it by unwrapping, which is the rejected alternative
above.

### Sparse-emit `isError`, never `isError: false`

Absent means success. Emitting `false` on every successful call adds a key to
every response for no signal, and MCP treats absence as success.

## Risks / Trade-offs

- **Clients that were silently treating failures as successes will start seeing
  failures.** That is the point, and such a client was already broken. Per the
  project's standing position, MCP surfaces are free to change because clients
  reload schemas every connection.
- **A tool that returns `error` for a non-failure would now be reported as
  failed.** Possible among those sites. Deliberately not audited here — the
  right response is to fix that tool's payload, not to weaken the rule. The
  change makes such cases *visible*, which is an improvement over uniform
  silence.
- **`error` becomes a reserved key in a tool payload.** A tool wanting to return
  an error-shaped field for some other reason would trip this. No current tool
  does; worth a line in `MCP_SERVER.md` rather than a mechanism.

## Migration Plan

None. No schema, no persisted state, no version gate — `isError` predates the
oldest protocol version this server negotiates, so it needs none of the
version-gating `structuredContent` and `resource_link` require.

## Open Questions

- Should `debug_get_log`-style tools that legitimately return an `error` field as
  *data* be checked? Believed none do; a grep during implementation settles it.
- Is `isError` worth surfacing in the app's own MCP connection view, so a user
  can see a failing tool without reading a log? Out of scope; noted because the
  data now exists.
