## 1. Establish the gaps with failing tests first

Each of these should FAIL against current `main` before its fix is written —
these are conformance claims, and a conformance test that was never seen red is
a claim nobody checked. All go in `tests/tst_mcpserver_protocol.cpp` as new
slots; do NOT add a test file (~1.4 s of build cost forever per file, ~ms per
slot).

- [ ] 1.1 Batch: POST an array of two `id`-bearing requests, assert a JSON array of two responses with matching `id`s
- [ ] 1.2 Batch: POST an array of notifications only, assert HTTP 202 with an empty body
- [ ] 1.3 Terminated session: `DELETE` a session, then POST with that ID, assert HTTP 404
- [ ] 1.4 Unrecognized session: POST with a never-issued ID, assert it is still served (pins the interop path the 404 must not eat — this one PASSES today and must keep passing)
- [ ] 1.5 Resource contents: read a resource at `2025-11-25`, assert no `structuredContent` key in any `contents[]` entry and that `text` still parses to the payload
- [ ] 1.6 Resource not found: `resources/read` an unknown URI, assert `code: -32002` and `data.uri`
- [ ] 1.7 Unknown tool: `tools/call` an unregistered name, assert `code: -32602`
- [ ] 1.8 SSE priming: open a stream, assert the first event carries an `id` and empty `data`, and that a `retry` field is present

## 2. Batch requests (MUST — 2024-11-05, 2025-03-26)

- [ ] 2.1 In `handleHttpRequest` (`src/mcp/mcpserver.cpp:434`), branch on `doc.isArray()` before the existing object path; leave the object path byte-identical
- [ ] 2.2 Process elements sequentially through the existing per-message logic; collect responses for `id`-bearing elements only
- [ ] 2.3 An element returning `_deferred` gets a JSON-RPC error in its slot naming the reason (see design). Log it — a batched confirmation request is a client doing something no client here has done, and the log is how we find out it happened
- [ ] 2.4 All-notification batch → HTTP 202, no body, matching the single-notification case at `:541`
- [ ] 2.5 Empty array → JSON-RPC `-32600` Invalid Request per JSON-RPC 2.0

## 3. Terminated-session 404 (MUST)

- [ ] 3.1 Add a bounded tombstone set of terminated session IDs to `McpServer` (cap + oldest-first eviction; state the cap and why at the declaration)
- [ ] 3.2 Record on `DELETE` (`src/mcp/mcpserver.cpp:629`) and in the expiry reaper (`cleanupExpiredSessions`)
- [ ] 3.3 In the non-`initialize` session lookup (`:456`), 404 a tombstoned ID BEFORE the auto-recovery branch at `:467` — and leave that branch reachable for unrecognized IDs
- [ ] 3.4 `initialize` carrying a tombstoned ID still creates a fresh session (it is the documented recovery move; 404ing it would strand the client)
- [ ] 3.5 Verify the `_deferred`/pending-confirmation cleanup on `DELETE` (`:634`) still runs before the tombstone is recorded

## 4. Resource contents (schema conformance)

- [ ] 4.1 Grep for readers of `structuredContent` inside `contents[]` — the tree, ShotServer JS, and the AI/MCP client code — before removing. Expected: none
- [ ] 4.2 Remove `content["structuredContent"] = resourceData;` from both the async and sync paths (`src/mcp/mcpserver.cpp:944`, `:983`) and the now-dead `emitStructured` computation in `handleResourcesRead`
- [ ] 4.3 Delete the test asserting `resources/read` omits `structuredContent` at 2024-11-05 (`tests/tst_mcpserver_protocol.cpp:705`). It does not over-specify — it pins a field the schema has never had, so leaving it would make the removal read as a regression
- [ ] 4.4 Confirm `tools/call` `structuredContent` is untouched: that one IS in the schema and IS version-gated correctly

## 5. Error codes

- [ ] 5.1 `-32002` + `data.uri` for a resource the registry does not serve (`src/mcp/mcpserver.cpp:952`, `:969`). Distinguish not-found from other read failures rather than mapping every failure to the new code
- [ ] 5.2 `-32602` for an unregistered tool (`:868`); leave "Tool is async" and "Access level insufficient" at `-32603` and say why at the site
- [ ] 5.3 Check `McpToolRegistry::callTool`'s `errorOut` strings (`src/mcp/mcptoolregistry.h:199-208`) are distinguishable without string-matching. If the caller has to compare error TEXT to pick a code, add a code/enum out-param instead — string-matching an error message is the thing that rots

## 6. SSE priming (SHOULD — 2025-11-25)

- [ ] 6.1 Emit `retry:` and a priming event (`id:` + empty `data:`) immediately after the SSE headers (`src/mcp/mcpserver.cpp:607`)
- [ ] 6.2 Per-session monotonic event-ID counter; every pushed event carries an `id` (`sendSseNotification`, `:228`)
- [ ] 6.3 Pick the `retry` interval against the existing 30 s keepalive probe so the two don't fight; state the reasoning in a comment
- [ ] 6.4 Do NOT implement `Last-Event-ID` replay — out of scope per the design, and a partial implementation is worse than none

## 7. Verify

- [ ] 7.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [ ] 7.2 Break each fix in turn and confirm its section-1 test goes red. Six fixes, six red proofs
- [ ] 7.3 **Live client matrix for the 404 — this is the change that can break a working setup.** Exercise `mcp-remote`, Claude Desktop, and both cloud connectors against an EXPIRED session, not just a `DELETE`. The auto-recovery comment claims `mcp-remote` cannot re-initialize itself; establish whether that is true rather than inheriting it
- [ ] 7.4 If `mcp-remote` wedges: fall back to tombstoning explicit `DELETE` only, and record the residual gap in the spec delta rather than quietly shipping a weaker rule than the spec says
- [ ] 7.5 Re-run the pre-fix probe over HTTP for each item (the `curl` initialize → `notifications/initialized` → call sequence) and diff against the recorded pre-change output
- [ ] 7.6 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 8. Document

- [ ] 8.1 `docs/CLAUDE_MD/MCP_SERVER.md`: resource-response shape (no `structuredContent` in `contents[]`, and why), the two error codes, batch acceptance, and what a 404 means to a client
- [ ] 8.2 Record the ONE deliberate non-conformance in the same place: the server binds all interfaces rather than localhost, because ShotServer exists to be LAN-reachable, mitigated by the `Origin` allowlist and the capability-URL gate. Written down so the next audit does not re-litigate it
- [ ] 8.3 Six independent commits, one per gap, so a client-breaking item can be reverted alone
- [ ] 8.4 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 8.5 Archive the change + spec sync as the final commit on the same PR
