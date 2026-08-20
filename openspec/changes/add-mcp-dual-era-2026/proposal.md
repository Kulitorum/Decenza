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
- **Every modern result carries `resultType`.** Required on all results:
  `"complete"` for an ordinary one, `"input_required"` for an MRTR interim
  result. We never return the second — see the non-goals — but the field is not
  optional, so `"complete"` is stamped on every modern result. Adopting the
  field is not adopting MRTR.
- **Modern results carry `serverInfo` in `_meta`.** A `SHOULD` in the revision
  (`io.modelcontextprotocol/serverInfo`), and cheap: it is the same identity the
  legacy handshake already reports, moved to where a stateless client can see
  it.
- **`ping`, `logging/setLevel` and `notifications/roots/list_changed` do not
  exist in the modern era.** We serve `ping` today and special-case it in
  session resolution; in the modern era it is simply an unknown method. Legacy
  keeps it.
- **Resource-update notifications move to `subscriptions/listen` for modern
  clients.** Our three SSE broadcasts (`machine/state`, `profiles/active`,
  `shots/recent`) are pushed today over the HTTP GET stream that the modern era
  removes — and so are `resources/subscribe`/`resources/unsubscribe`, which that
  one method replaces outright. It is not a transport swap: a client opts into
  named notification types (`resourcesListChanged`, `resourceSubscriptions`, …),
  the server acknowledges, and each notification is tagged with
  `io.modelcontextprotocol/subscriptionId`. Legacy keeps the GET stream and both
  subscribe verbs unchanged.
- **List results become cacheable — required, not optional.** `ttlMs` and
  `cacheScope` are mandatory on `tools/list`, `resources/list` and
  `resources/read` (and on `prompts/list` and `resources/templates/list`, which
  we do not serve), so clients stop re-fetching 96 tool descriptions per
  connection.
- **`resources/list` returns a deterministic order.** A `SHOULD` in the new
  revision. `tools/list` already satisfies it — it sorts by `(tier, name)`,
  landed with the tool-budget work for a different reason — but the resource
  registry is still a `QHash`, whose iteration order Qt randomizes per process.
  Applies to both eras and is worth taking regardless of the rest.
- **Modern-era error codes.** Resource-not-found becomes `-32602` for modern
  requests (`-32002` stays correct for legacy), and the MCP-reserved range
  moves to `-32020`+.
- **The official conformance suite becomes the verification gate.**
  `modelcontextprotocol/conformance` tests a server over an HTTP URL and is
  language-agnostic — no C++ SDK required, and none exists. It carries
  per-revision requirement sets including the 2026-07-28 stateless lifecycle,
  which is the only way to check this work against the spec before a real
  modern client exists.
- **The four legacy revisions are brought to conformance too, first.** We
  advertise `2025-11-25`, `2025-06-18`, `2025-03-26` and `2024-11-05`, and
  advertising a revision is a claim to implement it — a claim nothing has ever
  checked. Failures get fixed, not recorded: measuring legacy and shipping it
  red would convert an unknown defect into a known one and change nothing else.
  It lands before any modern code exists, because afterwards a legacy fix and a
  modern regression are indistinguishable.
- **Legacy behaviour changes only where it disagreed with the spec.** That is a
  narrowing of the original "keeps its exact behaviour" promise, taken
  deliberately: clients are written against the spec, so converging on it is
  converging on them. Every deviation the suite flags is re-derived rather than
  reflexively fixed — some exist because a real client needed them, and the
  auto-recovery branch that keeps `mcp-remote` working is one the suite will
  flag and we will keep.

Explicit non-goals: we do not adopt the Tasks extension, MRTR
(`input_required`), the MCP Apps extension, or OpenTelemetry trace context.
None is required of a server, and MRTR in particular does not replace our
in-app confirmation — that asks the user **at the machine**, not the client, so
it is not an elicitation and has no modern equivalent to migrate to. Note that
declining MRTR still leaves `resultType` mandatory: we always answer
`"complete"`, and never `"input_required"`.

Also a non-goal: **adopting an MCP library.** There is no official C++ SDK —
the ten official SDKs are TypeScript, Python, C#, Go, Java, Rust, Ruby, Swift,
PHP and Kotlin. Two third-party options do implement `2026-07-28`
(`signal-slot/qtmcp`, Qt-native and licence-compatible with this GPL-3.0
project; `GopherSecurity/gopher-mcp`, Apache-2.0), and both are worth reading.
Neither is adoptable here: each brings its own listener and HTTP layer, and
that is the one part this server cannot hand over — `McpServer` owns no socket,
ShotServer routes `/mcp` into it, and `McpRemoteAccess` adds a tokenized route
and a tunnel on top. What we take from the ecosystem is the schema and the
conformance suite, not the transport.

## Capabilities

### New Capabilities

- `mcp-dual-era-protocol`: how one endpoint serves both protocol eras — which
  era a request selects and how, `server/discover`, per-request version
  handling, `UnsupportedProtocolVersionError`, and the guarantee that legacy
  behaviour is unchanged.
- `mcp-stateless-requests`: what a modern request may rely on without a
  session — rate limiting, resource subscriptions, and the confirmation gate,
  each of which is keyed on the session today. Also what a modern result must
  carry that a legacy one does not: `resultType`, and the server's own identity.

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
  ordering (tools already have it; resources do not), `ttlMs`/`cacheScope` on
  list results.
- `src/mcp/mcpremoteaccess.cpp` — the second caller into `handleHttpRequest`;
  both eras must work over the remote surface, not just ShotServer's.
- `src/network/shotserver.cpp` — routes `/mcp` and is era-agnostic, but the
  modern subscription stream is a long-lived POST rather than a GET, which its
  socket handling has not seen before.
- `tests/tst_mcpserver_protocol.cpp`, `tests/tst_mcpserver_session.cpp` — the
  existing suite becomes the legacy-era regression net and must keep passing
  untouched; modern coverage is added beside it. Two carve-outs, both stated at
  the point they occur: the legacy-conformance work, where a test may turn out
  to have been asserting our bug, and the confirmation-gate change, which
  reworks a mechanism legacy uses.
- `docs/CLAUDE_MD/MCP_SERVER.md` — which era a client gets and why, and what
  a contributor must do to a tool so it works in both.
- **Wire-visible only to clients that opt into modern.** No client speaks
  `2026-07-28` to us today, so this ships dark and stays dark until one does.
