## 1. Session model: stateful vs ephemeral

- [x] 1.1 `McpSession::isStateful()` returns true only while a live SSE socket is held (`!m_sseSocket.isNull()`). No separate in-flight flag was needed: the reap decision is structurally taken only on the synchronous, non-`_deferred` completion path and is guarded by the pending-confirmation check, so an in-flight/deferred request is never reaped.
- [x] 1.2 `McpServer::statefulSessionCount()` counts sessions with a live SSE socket; used wherever the cap was measured against `m_sessions.size()`.

## 2. Concurrency accounting

- [x] 2.1 `createSession()` cap now compares `statefulSessionCount()` against `MaxSessions`, so ephemeral POST-only sessions never cause `-32000 "Too many sessions"`. Warning reworded to `Too many stateful sessions (N stateful, M total)`.
- [x] 2.2 Confirmed `MaxSseConnections` (4) still independently bounds concurrent SSE streams at the GET `/mcp` path — stateful sessions are therefore capped at 4 < 8, making `MaxSessions` a safety ceiling.

## 3. Stateless POST handling

- [x] 3.1 No code change needed: `initialize` already returns an `Mcp-Session-Id`, and the existing stale/unknown-session recovery path already serves an unmatched POST by materializing a session — the client-visible handshake is unchanged. Ephemeral sessions simply no longer count toward the cap.
- [~] 3.2 **Deliberately not done (minimal scope).** The "reuse sole session" / "auto-create on stale header" heuristics are now harmless (they only reduce session creation) and removing them carries regression risk for LAN `mcp-remote`. Left in place.

## 4. Reaping

- [x] 4.1 Ephemeral sessions continue to be released by the existing orphan sweep (idle bound 300 s — well below the 30-min stateful timeout), which already satisfies the spec's "reaped well before the stateful timeout" requirement. No per-request reaping added: it would churn conformant multi-POST handshakes and risk the `mcp-remote` reconnect path.
- [x] 4.2 Added a guard so the orphan sweep never reaps a session that currently holds a pending machine-start confirmation (a pre-existing latent gap: a cloud connector's momentary SSE closing could otherwise silently reset the confirmation).
- [x] 4.3 Confirmed the 30-min idle timer (`cleanupExpiredSessions`) remains only as a backstop; ephemeral cleanup is governed by the shorter orphan sweep.

## 5. Tests

- [x] 5.1 Added `testEphemeralSessionsDoNotExhaustPool` (`tests/tst_mcpserver_session.cpp`): 12 header-less `initialize`s (all ephemeral in the POST-only harness) all succeed and are retained — reproduces the ChatGPT/claude.ai churn and asserts no `-32000`.
- [~] 5.2/5.3/5.4 **Not added as unit tests.** The harness cannot hold an SSE stream open, so stateful-retention, in-flight-not-reaped, and stateful-cap-refusal paths aren't exercisable here; the stateful cap is also unreachable in practice (SSE cap 4 < `MaxSessions` 8). Covered instead by the live verification below.
- [x] 5.5 Full suite run via Qt Creator: **2780 passed, 0 failed, 0 warnings.**

## 6. Live verification (against the running fixed build on localhost:8888)

- [x] 6.1 Fired 15 back-to-back header-less `initialize`s → **15 succeeded, 0 "Too many sessions"** (pre-fix the 9th+ would be rejected). No-auth localhost endpoint connected cleanly.
- [x] 6.2 Verified the healthy path is intact: `initialize → notifications/initialized (HTTP 202) → tools/list` returns the full tool set (103 entries). Both cloud connectors were connected concurrently during the field logs that motivated the change.

## 6b. PR review follow-ups (PR #1494)

- [x] 6b.1 **Total-session backstop (Important).** Review found the fix removed the only ceiling on *total* sessions (`initialize` isn't rate-limited → tight-loop spammer could OOM). Added `MaxTotalSessions` (128) with LRU eviction of the least-recently-active *ephemeral* session (never stateful, never confirming), so churn can never reject a legitimate client. Safe: async responses are decoupled from the session object.
- [x] 6b.2 Unified the orphan sweep to `isStateful()`; removed the now-dead pending-confirmation reset in the removal loop (guard makes it unreachable — confirmed by two reviewers).
- [x] 6b.3 Fixed the `isStateful()` doc comment (ChatGPT opens SSE momentarily, not "never"; clarified client-name strings are illustrative) and added the `4 < 8` relationship to the cap comment.
- [x] 6b.4 Added `testSessionStatefulTracksLiveSseSocket` locking the `isStateful()` contract. Hoisted `statefulSessionCount()` to a local in the warning branch.
- [~] 6b.5 **Not added:** an eviction unit test would need to create 128+ sessions and would emit the (production-appropriate) `qWarning` the test harness forbids, or a friend-seam to lower the constant. Eviction is covered by the spec requirement + code review instead.
- [~] 6b.6 **Not changed:** pre-existing latent silent-drop in `cleanupExpiredSessions` (30-min timer resets a confirmation without responding) — practically unreachable (15 s auto-dismiss), not a regression this PR introduces; both reviewers advised not to block. Left as-is.

## 7. Docs

- [x] 7.1 Updated `docs/CLAUDE_MD/MCP_SERVER.md` Security §8 to describe stateful-vs-ephemeral sessions, that `MaxSessions` counts only stateful (SSE-backed) sessions via `statefulSessionCount()`, the ChatGPT/claude.ai per-request re-init behavior it accommodates, and the pending-confirmation reap guard.
