## Why

MCP revision `2026-07-28` removes protocol-level sessions and the
`initialize` handshake outright: every request carries its own version,
identity and capabilities in `_meta`, and servers advertise themselves through
a new `server/discover` RPC. The spec calls the two worlds **legacy**
(`2025-11-25` and earlier, handshake-based) and **modern**, and names a server
that speaks both **dual-era**.

The compatibility matrix leaves a server no choice about sequencing. A legacy
client on a modern-only server **fails with no recovery path** — the spec is
explicit that "legacy clients have no fall-forward mechanism", because the
handshake that would have negotiated a fallback is the thing that was removed.
A legacy client on a dual-era server works unchanged. So a server adopts modern
by *adding* it, never by switching, and the legacy half stays for years.

Doing it is worth it for reasons beyond conformance. Sessions are the single
largest source of accidental complexity in `McpServer` — a session pool with
three separate reapers, two ceilings, an eviction path, tombstones, and an
auto-recovery branch that exists *only* because cloud connectors re-initialize
per request and never echo the session header. That branch is this server
bending a session model around clients that never wanted one; the modern era
makes per-request the model. The revision also adds cacheable list results,
which matters here more than most: we serve 96 tools with paragraph-length
descriptions on every single connection.

## What Changes

- **A modern request path, served statelessly, alongside the existing one.**
  A request carrying modern per-request `_meta` is served per `2026-07-28`; an
  `initialize` request selects the existing legacy semantics. The spec permits
  both eras concurrently on one endpoint, which is what we do — one port, one
  handler, two eras.
- **`server/discover` is implemented.** Modern clients MAY call it to learn our
  supported versions up front; those that do not get
  `UnsupportedProtocolVersionError` (`-32022`) carrying the `supported` list and
  retry. Both routes must work.
- **Version handling moves off the session for modern requests.** `_meta`'s
  `io.modelcontextprotocol/protocolVersion`, and the `MCP-Protocol-Version`
  header, become the source of truth rather than a value negotiated once and
  stored.
- **Rate limiting gains a session-independent key.** Today the limiter counts
  against `McpSession::controlCallCount()`. A stateless request has no session
  to count against, so the modern path needs its own key before it can carry
  control-category tools at all. **This is the gating item, not a detail** — an
  unlimited control path on a machine that brews coffee is worse than the
  complexity sessions cost.
- **Resource-update notifications move to `subscriptions/listen` for modern
  clients.** Our three SSE broadcasts (`machine/state`, `profiles/active`,
  `shots/recent`) are pushed today over the HTTP GET stream that the modern era
  removes. Legacy clients keep the GET stream unchanged.
- **List results become cacheable.** `tools/list`, `resources/list` and
  `resources/read` gain `ttlMs` and `cacheScope`, so clients stop re-fetching
  96 tool descriptions per connection.
- **`tools/list` returns a deterministic order.** A `SHOULD` in the new
  revision, and one we fail today for an unrelated reason: the registry is a
  `QHash`, whose iteration order Qt randomizes per process. Applies to both
  eras and is worth taking regardless of the rest.
- **Modern-era error codes.** Resource-not-found becomes `-32602` for modern
  requests (`-32002` stays correct for legacy), and the MCP-reserved range
  moves to `-32020`+.
- **NOT BREAKING for any current client.** Every legacy revision we negotiate
  today keeps its exact behaviour. That is the point of the shape.

Explicit non-goals: we do not adopt the Tasks extension, MRTR
(`input_required`), the MCP Apps extension, or OpenTelemetry trace context.
None is required of a server, and MRTR in particular does not replace our
in-app confirmation — that asks the user **at the machine**, not the client, so
it is not an elicitation and has no modern equivalent to migrate to.

## Capabilities

### New Capabilities

- `mcp-dual-era-protocol`: how one endpoint serves both protocol eras — which
  era a request selects and how, `server/discover`, per-request version
  handling, `UnsupportedProtocolVersionError`, and the guarantee that legacy
  behaviour is unchanged.
- `mcp-stateless-requests`: what a modern request may rely on without a
  session — rate limiting, resource subscriptions, and the confirmation gate,
  each of which is keyed on the session today.

### Modified Capabilities

- `mcp-server`: adds deterministic list ordering and cacheable list results
  (both era-independent), and scopes the existing session requirements —
  termination, the concurrency limits, the pool bound, ephemeral reaping — to
  the legacy era, since the modern era has no sessions for them to govern.

## Impact

- `src/mcp/mcpserver.{h,cpp}` — era selection in `handleHttpRequest`, the
  modern request path, `server/discover`, per-request version handling. The
  legacy path is not rewritten.
- `src/mcp/mcpsession.h` — unchanged in behaviour; the rate-limit counter it
  owns gains a session-independent sibling for the modern path.
- `src/mcp/mcptoolregistry.h`, `src/mcp/mcpresourceregistry.h` — deterministic
  ordering, `ttlMs`/`cacheScope` on list results.
- `src/mcp/mcpremoteaccess.cpp` — the second caller into `handleHttpRequest`;
  both eras must work over the remote surface, not just ShotServer's.
- `src/network/shotserver.cpp` — routes `/mcp` and is era-agnostic, but the
  modern subscription stream is a long-lived POST rather than a GET, which its
  socket handling has not seen before.
- `tests/tst_mcpserver_protocol.cpp`, `tests/tst_mcpserver_session.cpp` — the
  existing suite becomes the legacy-era regression net and must keep passing
  untouched; modern coverage is added beside it.
- `docs/CLAUDE_MD/MCP_SERVER.md` — which era a client gets and why, and what
  a contributor must do to a tool so it works in both.
- **Wire-visible only to clients that opt into modern.** No client speaks
  `2026-07-28` to us today, so this ships dark and stays dark until one does.
