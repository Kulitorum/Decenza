## Context

Four revisions are advertised; the log shows two have never been requested by
any client, and the official conformance suite has no scenarios for either. The
question this change answers is not "are they used" — that is measured — but
"what do they hold up, and is removing them worth a wire-visible break for a
client we have not observed".

## Decisions

### The drop is justified by the batch path, not by the version list

Removing two strings from `supportedProtocolVersions()` buys nothing on its
own. What makes this worth doing is what those strings keep alive.

`handleJsonRpcBatch` is 136 lines, `willDeferResponse` is 13 more, and 36
references in the protocol test file exist to cover them. Batching is required
by exactly one revision: `2025-03-26`. It is absent from `2024-11-05`, was
removed in `2025-06-18`, stays absent from `2025-11-25`, and is absent again in
`2026-07-28`. Drop `2025-03-26` and every line of it is serving a revision
nobody speaks and no future revision restores.

It is also the most defect-prone code in the file, by its own record. Two
serious bugs have been found there and are documented at the site: a batched
`shots_delete` that deleted the row and then told the client it had been
refused, and a deferred handler writing a second complete HTTP response onto a
socket the batch had already answered. Both arise from the same structural
awkwardness — folding a per-message model into an array response — which does
not exist anywhere else in the server.

*Alternative considered:* keep batch dispatch as tolerance for a client that
batches regardless of its negotiated version, since we accept batches
unconditionally today. Rejected. No revision we would serve defines the shape,
nothing in the log has ever sent one, and "tolerance" here means retaining a
known bug class to serve a hypothetical caller.

### `2025-06-18` stays, and the reason is a measurement

`codex-mcp-client v0.147.0-alpha.6.5` negotiated `2025-06-18` three times in the
sampled window. It is one revision behind current and it is a client actually in
use here. Dropping it would be the only drop in this change with a known victim.

Stated explicitly because the intuition that prompted this work — "the clients
we care about are current" — is very nearly right and wrong in exactly one
place. The log is the reason we know which.

### The header-absent assumption moves, deliberately

The protocol says a server that receives no `MCP-Protocol-Version` header, and
has no other way to identify the version, should assume `2025-03-26`. Once we no
longer serve `2025-03-26` that instruction cannot be followed literally: it names
a version we would refuse.

The session default therefore becomes `2025-06-18`, the new floor. This is a
deviation from a SHOULD, taken knowingly, and it is the safe direction — the
floor emits strictly fewer optional fields than any revision above it, so a
client that omits the header is under-served rather than sent fields its
revision does not define.

### Version gates collapse only where the old arm is unreachable

`instructions` (gated `>= 2025-03-26`) and `title` on tools and resources
(gated `>= 2025-06-18`) become unconditional, because every revision still
served defines them.

`structuredContent` and `resource_link` are ALSO collapsed. Their gate reads
`>= 2025-06-18`, which is the floor, so it was as unreachable as `title`'s —
this was missed in the first pass of the change and the artifacts asserted the
opposite in four places, claiming the surviving revisions were "distinguishable
by them". They are not. Only `$schema` dialect stamping and `icons`
(`2025-11-25`) still gate anything, and only those keep their conditionals.

Recorded rather than quietly corrected: the false claim was written into
design.md, tasks.md, the docs and a code comment in one pass, and survived to an
open PR. It is the exact shape this project warns about — a stale premise
re-approved because nobody re-derived it.

The always-emitted text content block stays, and its comment is corrected. It
currently justifies itself as "the only payload that 2024-11-05 / 2025-03-26
clients read", which stops being true here — but `content[]` is required on a
tool result in every revision, `structuredContent` being additive rather than a
replacement. The block was never actually there for the old revisions; the
comment was wrong before this change and would have read as a reason to delete
it after.

### Dropping a revision from the NEGOTIABLE set is not dropping it from the wire

`2025-03-26` remains accepted as an `MCP-Protocol-Version` header value.

*Why:* the protocol designates it as the value to assume when the version cannot
be identified, and clients send it explicitly for that reason — it means "I do
not know", not "I want 2025-03-26". Treating it as a version request and
refusing it converted the ecosystem's own compatibility mechanism into a hard
400 for any client using it.

This was not predicted here; the conformance suite found it within a minute of
the change being built, on the same scenario that found the earlier
header-handling defect. It is the clearest argument so far for the suite being a
gate rather than a report.

*What it does NOT mean:* the revision is still not negotiable, and a client
sending the sentinel gets the session's version, not 2025-03-26's semantics. In
particular it does not resurrect batching, which only that revision defines.

*Consequence for the dual-era change:* its rule that "a supported header wins"
must not honour the sentinel as a version — the sentinel maps to the session, a
supported header maps to itself. Two different behaviours for two different
kinds of header value.

## Risks / Trade-offs

- **A client requesting a dropped revision breaks.** It receives `2025-11-25`
  from negotiation and, per the protocol, should disconnect if it cannot speak
  it. No observed client does this → the mitigation is that this lands as its
  own change, so the break is attributable and revertible without disturbing
  anything else. `mcp-remote` is the wildcard: the auto-recovery branch exists
  for it and its negotiated revision is not in the sampled log.
- **A client that batches anyway now gets an error.** Today it would be served.
  Accepted: no revision we serve defines batching, and an explicit error is a
  better outcome than silently retaining the bug class that shape has produced
  twice.
- **The log is one device over a bounded ring buffer.** It is the best evidence
  available and it is not a census. A client that connects rarely could be
  absent from it — which is an argument for landing this separately and
  watching, not for keeping four revisions indefinitely.
