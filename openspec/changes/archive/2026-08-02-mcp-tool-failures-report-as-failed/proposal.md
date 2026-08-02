# MCP tool failures must not report as protocol success

## Why

Every MCP tool in Decenza reports failure by returning an object with an `error`
key. None of those failures reaches the client as a failure.

`McpServer::sendJsonRpcResponse` (`src/mcp/mcpserver.cpp:1346`) decides between a
JSON-RPC `error` and a JSON-RPC `result` by testing `result.contains("error")`.
That test is correct for plain JSON-RPC methods. It is **dead for tool calls**,
because the tool's payload has already been wrapped by `buildToolCallResponse`
(`:303`) into `{content, structuredContent}` — so the `error` key is one level
down, inside the content, and the wrapper never contains it. The response ships
as `result`, with no `isError: true` either.

Consequence: a client asking for shots on a database that failed to open, or a
tool whose SQL is broken, is told the call **succeeded**. A spec-conformant MCP
client has no way to know otherwise. Only a model that happens to read the
rendered JSON text notices the word "error" — which is exactly the "reports
success having silently used something else" failure mode CLAUDE.md warns about,
one layer down.

This was found while fixing a real instance: the `shots_list` tool returned
`shots: []` beside a correct non-zero `total` for every call after an ambiguous
column broke its query. That specific bug is fixed
([#1752](https://github.com/Kulitorum/Decenza/pull/1752)), and the tool now sets
`result["error"]` — but the error still leaves the server labelled as success.

The codebase already knows the right shape and applies it at exactly one site:
the confirmation-denial path sets `isError = true` on the wrapper by hand
(`:1189`). Nothing generalizes it.

## What Changes

- **A tool payload carrying `error` produces a failed tool call.** The
  wrap step recognizes the key and marks the wrapper `isError: true`, so the
  signal survives wrapping instead of being buried by it.
- **The `error` text stays in `content`**, where a model can read it. This is a
  protocol-level addition, not a replacement — MCP models tool failures as a
  successful JSON-RPC response carrying `isError`, reserving JSON-RPC `error` for
  protocol faults (unknown method, malformed request). Tools must NOT start
  returning JSON-RPC errors.
- **The hand-rolled `isError` at the denial site is folded into the shared
  path**, so there is one place that decides what a failed tool call looks like.
- **The dead branch is documented, not deleted.** `sendJsonRpcResponse`'s
  `contains("error")` test stays correct for non-tool methods; it gains a comment
  saying why it cannot fire for tool calls, so the next reader does not "fix" it
  by unwrapping.

Explicit non-goals: no tool's error TEXT changes, no tool gains or loses an error
path, and no error key is renamed. This changes how failures are labelled on the
wire, nothing else.

## Capabilities

### New Capabilities

- `mcp-tool-error-reporting` — how a tool signals failure and how that failure is
  represented on the wire.

### Modified Capabilities

None. No existing spec under `openspec/specs/` describes the tool-call response
envelope.

## Impact

- `src/mcp/mcpserver.cpp` — `buildToolCallResponse`, the denial path at `:1189`,
  and a clarifying comment on `sendJsonRpcResponse`.
- `tests/tst_mcpserver_protocol.cpp` — the natural home; it already exercises the
  response envelope and protocol-version gating.
- `docs/CLAUDE_MD/MCP_SERVER.md` — the error convention belongs in the data
  conventions section.
- **No tool file changes.** All ~283 error-returning sites across
  `src/mcp/mcptools_*.cpp` keep
  their current shape; that is the point of fixing it centrally.

Wire-visible behaviour change for MCP clients. Per the project's standing
position, MCP tool surfaces are free to change — clients reload schemas every
connection — and a client that was treating failures as successes was already
broken.
