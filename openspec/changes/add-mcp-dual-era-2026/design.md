## Context

`McpServer::handleHttpRequest` is the single entry point for both callers —
ShotServer's `/mcp` route and `McpRemoteAccess`'s tokenized remote route. It
currently implements one thing: the handshake-based era — `2025-06-18` and
`2025-11-25` since #1840 dropped the two older revisions — with `m_sessions` at
its centre. That pool is referenced 23 times
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
- **Every legacy revision conforms to the spec it claims to implement**, and is
  otherwise unchanged. This is deliberately not the "byte-identical to today"
  goal this document first stated — see the decision below on why measuring
  legacy and then leaving it red was the wrong shape. Outside the deviations
  section 0 fixes or knowingly keeps, the existing protocol and session test
  files are the regression net and must pass untouched.
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

*Key:* the peer address, on both routes, with a per-minute window. This is not
a new mechanism — `McpRemoteAccess::failedTokenOverLimit` already keys a
per-minute budget on `socket->peerAddress()` for failed token attempts, and the
modern limiter is the same shape applied to a different budget.

*Why the peer address and not the remote-access token:* there is exactly one
`remoteMcpToken` for the whole app, so every remote caller presents the same
one. Keying on it would produce a global limiter for the entire remote route —
precisely the starvation the per-session counter was shaped to avoid.

*On the NAT objection:* ShotServer's route is LAN-only, where peers are
distinct devices, and the remote route's callers arrive over the tunnel with
distinct addresses. A shared key would require two callers behind one NAT on
the LAN route, which is not a shape this server sees.

*Alternative considered:* a global limiter with no key. Rejected for the
starvation reason above.

*Alternative considered:* let modern requests borrow a hidden session keyed on
transport identity. Rejected — that is a session by another name, reintroduces
every reaper, and the resulting behaviour would be neither era's.

### The confirmation gate gets its own identity, and both eras use it

`PendingConfirmation` mints a `confirmationId` of its own. The QML dialog is
handed that id and echoes it back to `confirmationResolved`. **Legacy moves
onto the same mechanism**; the modern era does not get a parallel one beside
it.

*Why:* the `sessionId` stored on `PendingConfirmation` is not a session lookup
and never was. Nothing calls `findSession` on it. It is an opaque correlation
token round-tripped through QML, plus a value handed to `sendJsonRpcResponse`
for the response header, plus a string three cleanup sites compare against a
dying session's id. Only the second and third of those are genuinely about
sessions. Giving the confirmation its own id separates the correlation concern
from the session concern, and leaves `sessionId` meaning one thing: which
legacy session owns this, empty when none does.

*Why not simply refuse confirmation-gated tools on the modern path:* that is a
defensible interim posture and a bad destination. The end state of this plan is
legacy dropped; on that day, refusal would make `machine_start_*` unreachable
to every client. A rule that becomes unacceptable exactly when the plan
completes is not the design, and building the parallel-mechanism version first
means writing the migration twice.

### A pending confirmation's lifetime is scoped to its socket

`QTcpSocket::disconnected` abandons the pending confirmation, in both eras.

*Why:* it states the actual invariant — a confirmation whose requester is gone
cannot be meaningfully answered — and it is event-based, so it needs no timer.
It is also the only backstop available in the modern era, which has no session
and therefore no idle reaper.

*Consequence for legacy, which is an improvement rather than a cost:* today a
dead socket is noticed only when the user finally answers the dialog, and the
session reaper is what eventually clears an abandoned one up to thirty minutes
later. After this change the disconnect clears it immediately and the reaper
becomes a redundant secondary trigger.

*Consequence for the regression net:* this deliberately changes a mechanism
legacy uses, so `tst_mcpserver_session.cpp` may need updating in the second PR.
That is a real weakening of the "existing tests pass untouched" rule, which
applies in full to the first PR and is knowingly relaxed here for this one
mechanism.

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

### The official conformance suite is the verification gate, not our tests alone

`modelcontextprotocol/conformance` is run against the modern path, and passing
it is what "done" means for the second PR.

*Why:* no real modern client speaks to us, so without it the only evidence this
revision is implemented correctly is a test suite written by the same reading of
the spec that produced the implementation — an error in that reading is
invisible to both. The suite is language-agnostic by construction (it drives a
server over an HTTP URL, and drives clients by command line), so the absence of
a C++ SDK does not exclude us from it, and it carries per-revision requirement
sets that fix which scenarios apply to `2026-07-28` specifically.

*Why not adopt a library instead:* there is no official C++ SDK; the ten
official SDKs are TypeScript, Python, C#, Go, Java, Rust, Ruby, Swift, PHP and
Kotlin. Two third-party implementations do carry `2026-07-28` — `qtmcp`
(Qt-native, and its GPL-3.0-only arm matches this project's licence) and
`gopher-mcp` (Apache-2.0) — but both own their listener and HTTP layer, which is
the part this server structurally cannot delegate: `McpServer` has no socket of
its own, ShotServer routes `/mcp` into it, and `McpRemoteAccess` adds a
tokenized route and a tunnel above that. Read them for the shapes; take the
schema and the conformance suite as the actual dependency.

*Consequence:* `schema/2026-07-28/schema.json` is the source of truth for field
names and shapes. Where this design and that schema disagree, the schema wins —
this document was drafted from the revision's prose, and the prose is a summary.

### Legacy conformance is brought to green, not measured and left

The suite is run against the four advertised legacy revisions first, and the
failures it finds are fixed. It is not a baseline taken to protect the modern
work from blame.

*Why:* `supportedProtocolVersions()` advertises four revisions, and advertising
one is a claim to implement it. Nothing has ever checked that claim. Running the
suite, writing down what is red and shipping anyway would convert an unknown
into a known defect and change nothing else — the worst of the three available
outcomes, because it puts the failure in a document instead of in the code.

*Why first, before any modern code exists:* a legacy fix landed afterwards
cannot be distinguished from a modern regression. The entire change rests on
being able to say legacy did not move, and that sentence is only checkable
against a legacy baseline that is already green.

*What this costs:* the "byte-identical" goal, which is why it has been restated
above. Legacy behaviour changes wherever it disagreed with the spec. That is a
real risk to a working client and it is taken deliberately: clients are written
against the spec, so converging on the spec is converging on them.

*What it does NOT buy, measured rather than assumed:* the suite covers two of
the four revisions we advertise. `--spec-version 2024-11-05` is rejected as an
unknown version, and `2025-03-26` selects zero scenarios; only 2025-06-18 and
2025-11-25 have any. So a green run says nothing about half the advertised list,
and the two oldest revisions remain verified by our own tests alone. Say that
plainly wherever the result is reported — "legacy conforms" would be a broader
claim than the evidence.

*And what it did not find:* every deviation this plan predicted the suite would
flag — the auto-recovery branch, un-tombstoned reaper sessions, absent
`Last-Event-ID` replay, event IDs that do not encode their stream, batch
accepted at every revision — went unchallenged. One defect was found, and it was
not on the list. Read that as the suite not probing those areas, not as their
being cleared.

### A deviation the suite flags is re-derived, not automatically fixed

Each conformance failure is triaged into a defect or a deliberate deviation, and
a deliberate one is re-argued from its written reason rather than defended or
silently "fixed".

*Why:* some deviations here exist because a real client needed them, and the
auto-recovery branch is the one that must survive contact with this rule. It is
more permissive than the spec on purpose: `mcp-remote` cannot re-initialize
itself, so answering it strictly leaves a client, in that code's own words,
permanently broken until restart. The suite flagging it is the suite being right
about the spec and wrong about the client. "Conformance said so" is not on its
own a reason to change behaviour.

*Why not the reverse — keep every deviation that has a comment:* a written
reason is evidence the deviation was once deliberate, not evidence it is still
correct. Several were written against a design that has since moved. Re-deriving
each one is the same discipline this project applies to a stale optimisation
comment, and the outcome is recorded at the site either way, so the next person
inherits the argument rather than the conclusion.

### `ping` disappears in the modern era, and that is not a compatibility break

`ping`, `logging/setLevel` and `notifications/roots/list_changed` are removed by
the revision. Modern requests naming them get "method not found"; legacy keeps
all three exactly as today.

*Why it is worth stating:* `ping` is not just a handler here, it is one of the
two methods exempted from the "session not initialized" check in
`resolveSessionForMessage`. That exemption is legacy-only by construction, so
nothing needs undoing — but a reader who finds the exemption and the modern
path's rejection at different times will otherwise read them as contradicting
each other.

### A supported header selects a version; the compatibility sentinel selects nothing

`2025-03-26` in the header and a supported version in the header take DIFFERENT
paths, and collapsing them would be wrong in a way that is easy to miss.

*Why:* #1840 kept `2025-03-26` accepted after making it non-negotiable, because
it is the value the spec tells a server to assume when no header arrives and
clients emit it for the same reason. It means "I do not know". A supported
header means "answer me under this". So the sentinel maps to the SESSION's
version and a supported header maps to ITSELF.

*What breaks if they are merged:* honouring the sentinel as a version would have
the server claim to serve `2025-03-26` semantics — batching among them — which
it no longer implements at all. Answering the supported header with the session's
version instead would reintroduce the defect the conformance suite caught.

*Consequence for the modern era:* the same asymmetry has to survive into the
per-request version handling. `_meta`'s protocol version is a selection, not a
sentinel, so it never takes the sentinel path.

## Risks / Trade-offs

- **Era misdetection breaks a working client** → The ambiguous case defaults to
  legacy, and the existing test files must pass unmodified. Any diff to them is
  a signal the legacy path moved, not that a test needed updating.
- **Fixing legacy conformance breaks a working client**, which is a sharper
  version of the same risk and the price of section 0 → Three defences, in
  order. A flagged deviation is re-derived before it is touched, so the ones
  that exist to keep a real client working are kept. The live end-to-end test
  over a real legacy client (task 9.4) is what would catch a fix that is
  spec-correct and client-fatal. And section 0 lands alone, first, so a
  regression it causes is attributable to it rather than to the modern path.
  Note what is NOT a defence: the existing test files, which section 0 is
  permitted to change — a test asserting behaviour the spec contradicts was
  asserting our bug, and that is exactly the case where the regression net is
  the thing being corrected.
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
  → The official conformance suite is the answer to this and is why it is a gate
  rather than a nice-to-have; our own tests alone would only re-assert our own
  reading of the spec. Even so, do not claim field validation the way the legacy
  path now has it: conformance is not a real client.
- **The design was drafted from the revision's prose, and prose summarises**
  → Five requirements were missing from the first draft and were found only by
  reading the changelog against the code: mandatory `resultType`, the removal of
  `ping`, `ttlMs`/`cacheScope` being required rather than optional, the opt-in
  shape of `subscriptions/listen`, and `serverInfo` in each result's `_meta`.
  Build against `schema.json`, and expect this list not to be complete either.
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

- **When does legacy get dropped?** Not this change, and not soon. Worth a note
  in `MCP_SERVER.md` recording that the answer is "when no client needs it",
  so the question is not re-litigated every time the session code annoys someone.

Two questions that were open here have been decided and moved into Decisions
above: the rate-limit key, and whether the confirmation gate needs a modern
form.
