## Context

`McpServer::handleHttpRequest` is the single entry point for both callers —
ShotServer's `/mcp` route and `McpRemoteAccess`'s tokenized remote route. It
currently implements one thing: the handshake-based era, `2024-11-05` through
`2025-11-25`, with `m_sessions` at its centre. That pool is referenced 23 times
in the file and carries a session-idle reaper, an orphan reaper inside
`findOrCreateSession`, `MaxTotalSessions` eviction, two ceilings, a tombstone
set, and an auto-recovery branch whose own comment records that it exists
because `mcp-remote` cannot re-initialize itself.

`2026-07-28` does not extend that model, it deletes it. There is no handshake:
each request declares its version in `_meta` (mirrored in the
`MCP-Protocol-Version` header), and the server accepts or rejects each request
independently. The spec's terms are **legacy** for handshake-based revisions,
**modern** for per-request-metadata ones, and **dual-era** for an
implementation serving both — which it explicitly permits "concurrently on the
same endpoint or process".

The constraint that decides the whole shape is one row of the compatibility
matrix: **legacy client + modern-only server fails, with no recovery.** The
handshake that would have negotiated a fallback is what was removed. So modern
is added beside legacy, never in place of it, and the legacy half stays until
no client needs it — a horizon of years, backed by the revision's new
twelve-month minimum deprecation windows.

Three pieces of our server are keyed on the session and therefore have no
modern equivalent yet: the rate limiter (`McpSession::controlCallCount()`), the
SSE resource-update broadcast (an HTTP GET stream the modern era removes), and
the in-app confirmation gate (`m_pendingConfirmation`, which stores a
`sessionId`). Those three, not the protocol plumbing, are the actual work.

## Goals / Non-Goals

**Goals:**

- One endpoint serves both eras, with era selected per request.
- Every legacy revision behaves **byte-identically** to today. The existing
  protocol and session test files are the regression net and must pass
  untouched.
- Control-category tools are rate-limited in the modern era before they are
  reachable in the modern era. Not after.
- Both eras work over **both** callers — `McpRemoteAccess` as well as
  ShotServer. The remote route is a separate entry that has been missed before.

**Non-Goals:**

- **Deleting the session machinery.** Legacy needs all of it. The simplification
  the modern era offers is only realised the day legacy is dropped, which this
  change does not do and should not pretend to.
- **Tasks, MRTR (`input_required`), MCP Apps, OpenTelemetry trace context.**
  None is required of a server. MRTR in particular is not a replacement for our
  in-app confirmation: that asks the user *at the machine*, not the client, so
  it is not an elicitation and there is nothing to migrate.
- **Advertising `2026-07-28` in `supportedProtocolVersions()` until the modern
  path is complete.** The list is a promise; adding to it early makes us lie to
  `server/discover` callers.
- **Removing `-32002` for legacy resource-not-found.** It is correct for every
  revision we currently negotiate. The modern era uses `-32602`.

## Decisions

### Era is selected by the request, not by a mode flag

A request selects its era the way the spec says a dual-era server should read
it: an `initialize` request means legacy; a request carrying modern per-request
`_meta` means modern. No configuration switch, no separate port, no build flag.

*Why:* an era flag would have to be set before the server knows who is calling,
and both callers multiplex clients of unknown era over one endpoint. It would
also create a state no test could cover cheaply — every case doubled.

*Consequence:* the discriminator must be cheap and unambiguous, because it runs
before anything else. The decisive signal is the modern-only header set
(`Mcp-Method`, `Mcp-Name`) plus `_meta.io.modelcontextprotocol/protocolVersion`
in the body; legacy requests carry neither. `MCP-Protocol-Version` alone is NOT
a discriminator — legacy has sent it since `2025-06-18`.

### An ambiguous request is legacy

If the era cannot be determined, serve legacy.

*Why:* the failure modes are not symmetric. Mis-routing a legacy request to the
modern path breaks a client that works today; mis-routing a modern request to
the legacy path produces a `400`/JSON-RPC error, which is exactly what the
spec's client-side detection expects to see and recover from — "if the body is
empty or is not a recognized modern JSON-RPC error, fall back to `initialize`".
The default that costs a working client nothing is legacy.

### Rate limiting is the gate on the whole modern path

Control- and settings-category tools stay **unreachable** in the modern era
until a session-independent limiter exists.

*Why:* `session->controlCallCount()` is the only thing standing between a
client and unbounded `machine_start_*` calls. A stateless request has no
session to count against. Shipping the modern path without solving this first
would put an unlimited control surface on a machine that heats water to 90 °C
and this is not a theoretical objection — the limiter was added because it was
needed.

*Preferred key:* the transport-level caller identity each route already has —
the remote-access token for `McpRemoteAccess`, the peer address for ShotServer.
Both are per-caller and neither requires retained protocol state.

*Alternative considered:* a global limiter with no key. Rejected — one client
could then starve another, which is the failure the per-session counter was
shaped to avoid.

*Alternative considered:* let modern requests borrow a hidden session keyed on
transport identity. Rejected — that is a session by another name, reintroduces
every reaper, and the resulting behaviour would be neither era's.

### Modern subscriptions are additive, and may lag

`subscriptions/listen` (long-lived POST) is implemented *after* the
request/response path works, and until it is, a modern client simply has no
resource notifications.

*Why:* it is the only piece requiring new socket handling — ShotServer holds
a socket open for SSE on GET today, and a long-lived POST stream is a shape it
has not seen. Sequencing it last keeps that risk off the critical path, and a
modern client with no notifications still works; it polls.

### `server/discover` is implemented, but clients are not assumed to call it

Both routes must work: discovery up front, and inline invocation that hits
`UnsupportedProtocolVersionError` (`-32022`) and retries from the `supported`
list.

*Why:* the spec makes `server/discover` a server MUST and a client MAY. Testing
only the discovery path would leave the more likely one uncovered.

### Deterministic ordering and cacheable results land first, and land for both eras

`tools/list` ordering and `ttlMs`/`cacheScope` are independent of era selection.

*Why:* they are the only parts of this revision with a user-visible payoff that
does not require a modern client to exist — 96 tool descriptions stop being
re-fetched, and the ordering fix is worth taking on its own. Landing them first
also means the big change is not carrying small wins hostage.

*Note on the ordering bug:* the registry is a `QHash`, and Qt randomizes the
hash seed per process unless `QT_HASH_SEED=0`
(`qtbase/src/corelib/tools/qhash.cpp:121-127`, `:178`). The order is therefore
stable within a run and different across restarts — which is the worst case for
a client cache, because nothing looks wrong until you compare two runs.

## Risks / Trade-offs

- **Era misdetection breaks a working client** → The ambiguous case defaults to
  legacy, and the existing test files must pass unmodified. Any diff to them is
  a signal the legacy path moved, not that a test needed updating.
- **The modern path doubles the surface `McpServer` must get right**, and this
  is a file where a review already found a batch element being dispatched before
  its refusal → Share the dispatch layer (`handleJsonRpc` and below) between
  eras; only the envelope — version resolution, session lookup, response
  framing — forks. If a fix has to be made twice, the fork is in the wrong
  place.
- **Modern control tools ship unlimited** → Structural, not procedural: the
  category gate refuses control/settings on the modern path until the limiter
  exists, so the unsafe intermediate state cannot be reached by forgetting.
- **`subscriptions/listen` needs long-lived POST handling in ShotServer** →
  Sequenced last; a modern client without it degrades to polling rather than
  breaking.
- **We implement a revision no client speaks to us yet, so bugs sit undiscovered**
  → It ships dark by construction. Treat the test suite as the only evidence
  until a real modern client appears, and do not claim field validation the way
  the legacy path now has it.
- **The simplification everyone wants does not arrive with this change** →
  Deleting the session machinery needs legacy *dropped*, which is a separate
  decision years out. Stated here so the change is not undersold as a cleanup.

## Migration Plan

No data migration, no persisted state, no settings. The rollout is ordering,
not deployment:

1. Era-independent wins (ordering, cacheable results) — shippable alone.
2. Modern envelope: era detection, per-request version handling,
   `server/discover`, `UnsupportedProtocolVersionError`. Read-category tools
   only.
3. Session-independent rate limiter; control/settings categories opened on the
   modern path.
4. `subscriptions/listen`.
5. `2026-07-28` added to `supportedProtocolVersions()` — last, because that list
   is what `server/discover` promises.

Rollback is per-step; nothing before step 5 is visible to a client that does not
already speak modern.

## Open Questions

- **What is the right rate-limit key for ShotServer's route?** The peer address
  is available, but a LAN behind one NAT collapses to a single key. The remote
  route has a token and is unambiguous. Worth deciding against real client
  shapes rather than in the abstract.
- **Does the in-app confirmation gate need a modern form at all?** It stores a
  `sessionId` to route the answer back. A stateless caller has none, so either
  the modern path refuses confirmation-gated tools (defensible — they are the
  `machine_start_*` family) or the gate learns a request-scoped handle. Not
  decided here; step 2 excludes those tools anyway.
- **When does legacy get dropped?** Not this change, and not soon. Worth a note
  in `MCP_SERVER.md` recording that the answer is "when no client needs it",
  so the question is not re-litigated every time the session code annoys someone.
