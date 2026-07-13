## Context

The MCP server (`src/mcp/mcpserver.cpp`) allocates a durable `McpSession` for every `POST` `initialize` and stores it in `m_sessions` (a `QHash`). `createSession()` rejects new sessions once `m_sessions.size() >= MaxSessions` (8). Two reapers free slots: an opportunistic orphan sweep inside `createSession()` (removes SSE-less sessions that either dropped an SSE socket or have been idle > `OrphanIdleSeconds` = 300 s), and a 60 s timer (`cleanupExpiredSessions()`) that expires sessions idle > `SessionTimeoutMinutes` = 30 min.

Live logs show the ChatGPT connector (`client="openai-mcp"`) re-runs `initialize` on essentially every request, never echoes the `Mcp-Session-Id` header, and opens its SSE stream (`GET /mcp`) only *momentarily* — it connects, receives the tool result, and disconnects within ~0.4 s:

```
Created session …  initialize client="openai-mcp"
SSE client connected, total: 1
SSE client disconnected, remaining: 0
Removing orphaned session …   (only on the *next* createSession)
```

Because each churned session counts toward `MaxSessions` until a later sweep reaps it, a burst wedges the pool at 8 and every other client — including a well-behaved Claude session — gets `-32000 "Too many sessions"` for up to five minutes. This is a documented, repeatedly-regressed ChatGPT protocol violation; the ecosystem best practice is stateless-first: retain per-client state only for clients that keep an SSE stream open for push notifications.

The distinguishing signal is therefore **a persistently-open SSE socket**, not merely "did an SSE ever attach." A real dial-in subscriber — LAN `mcp-remote` / Claude Desktop receiving `decenza://shots/recent` pushes — holds its SSE stream open continuously. Both cloud connectors do not: `client="openai-mcp"` opens SSE only for a single request/response window, and `client="Anthropic/ClaudeAI"` never opens SSE at all (pure POST). Both re-`initialize` per turn without echoing the session header, so both are correctly classified ephemeral under this signal.

## Goals / Non-Goals

**Goals:**
- A per-call re-initializing, header-less client (ChatGPT) can never exhaust the session pool or cause another client to be rejected.
- Clients that keep an SSE stream open (dial-in push subscribers) retain full stateful behavior: capabilities, subscriptions, notifications.
- No change to the wire protocol, the handshake a client sees, tool/resource behavior, or any access-level / confirmation / origin / protocol-version / rate-limit gate.

**Non-Goals:**
- Changing the remote-access security model (capability URL, token rotation, gates) — untouched.
- Adding true horizontal/serverless statelessness or persisting session state across process restarts.
- "Fixing" ChatGPT's behavior — we adapt to it, we do not depend on it changing.
- Raising `MaxSessions` as the fix (treats the symptom, not the cause).

## Decisions

### Decision 1: A session is *stateful* iff it currently holds a live SSE socket

Discriminate on `McpSession::sseSocket() != nullptr` (present/live), not on the historical `hadSseSocket()`. A session with no live SSE socket — whether it never opened one or its SSE has closed — is **ephemeral**. This exactly separates a persistent push subscriber (SSE held open) from ChatGPT's momentary per-call SSE (opened, drained, closed).

*Alternative considered — discriminate on `hadSseSocket()`:* rejected, because ChatGPT *does* briefly open an SSE stream, so "ever had SSE" would misclassify its churn as stateful.

### Decision 2: `MaxSessions` counts only stateful (live-SSE) sessions

The cap in `createSession()` (and any future SSE-establishment path) compares against the count of sessions with a live SSE socket, not `m_sessions.size()`. Ephemeral sessions never contribute to the count and can never trigger `-32000 "Too many sessions"`. Concurrent SSE streams remain independently bounded by the existing `MaxSseConnections` (4).

### Decision 3: The POST path serves statelessly when no stateful session matches

None of the exposed tools require cross-request server-side state; the only cross-request feature is resource subscription, which requires an open SSE stream anyway. So for any `POST` (including `initialize`) whose `Mcp-Session-Id` does not match a live stateful session, the server materializes a transient session context, serves the request, still returns an `Mcp-Session-Id` in the response (so the client-visible handshake is unchanged), and does not retain it in the counted pool. This lets the fragile "reuse the sole session / auto-create on stale header" heuristics in `handleHttpRequest()` be simplified or removed.

### Decision 4: Ephemeral reaping is event-based, not timer-based

Per the project rule against timers-as-guards, an ephemeral session is released on concrete events — its SSE socket closing and its last in-flight request/response completing — rather than by shortening an idle timer. Reaping is gated by "no live SSE **and** no in-flight request **and** no pending confirmation for this session," so a slow tool call or a held machine-start confirmation is never cut. The 30-min idle timer remains only as a backstop for stateful sessions.

## Risks / Trade-offs

- **ChatGPT may trigger an auth flow against a server it perceives as stateless** → We still emit an `Mcp-Session-Id` on every `initialize` response, so the client-visible handshake is byte-for-byte what it is today; only server-side retention changes. Must still be verified live through the ChatGPT connector before merge (no-auth capability URL must connect without demanding OAuth).
- **A conformant POST-only client that relies on server-retained state between POSTs would break** → No such state exists for our tools; subscriptions (the only stateful feature) require SSE. Verified against the tool/resource surface.
- **In-flight ephemeral request reaped mid-call** → Prevented by the event-based reap gate (no reap while a request is in flight or a confirmation is pending for that session).
- **Pending in-app confirmation on an ephemeral session** (machine-start holds the socket) → The existing `m_pendingConfirmation` check is preserved in the reap path so a confirmation-bearing session is never dropped.
- **Regression surface in a security-adjacent module** → Mitigated by unit tests covering the stateful/ephemeral split, cap accounting, and reap gating, plus a live ChatGPT + Claude concurrent test.

## Migration Plan

Pure behavior change: no settings, schema, or data migration. Ships in the app binary. Rollback is a straight revert of the `src/mcp/` changes; no persisted state is affected. Update `docs/CLAUDE_MD/MCP_SERVER.md` (session/limits section) in the same change.

## Open Questions

- **Resolved by live log (2026-07-12):** captures of both connectors under real traffic confirmed neither holds a persistent SSE stream. `client="openai-mcp"` opens SSE momentarily (every `SSE client connected` followed within <1s by `disconnected`, live-SSE count never staying >0); `client="Anthropic/ClaudeAI"` emits no `SSE client connected` lines at all — it is pure POST — and re-`initialize`s per turn (three questions → three fresh sessions, no header reuse). Persistent held-open SSE for resource push is exclusively a LAN `mcp-remote` / Claude Desktop behavior. This confirms Decision 1 and adds a concrete corrective: the existing SSE-disconnect handler deliberately *keeps the session alive* for a possible reconnect (`mcpserver.cpp` ~line 595) — but remote clients re-`initialize` fresh rather than reconnecting, so that retained session simply rots until the orphan sweep. The fix must reap a session promptly when its SSE closes (gated by no in-flight request / confirmation); a genuine persistent holder (mcp-remote) keeps its socket open and is therefore never in the closed state, so it is unaffected.
- Should ephemeral sessions be bounded by a generous soft cap with LRU eviction purely as a memory backstop against a pathological client, given they are reaped promptly on request completion?
