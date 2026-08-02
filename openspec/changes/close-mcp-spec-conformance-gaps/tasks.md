## 1. Establish the gaps with failing tests first

Each of these should FAIL against current `main` before its fix is written —
these are conformance claims, and a conformance test that was never seen red is
a claim nobody checked. All go in `tests/tst_mcpserver_protocol.cpp` as new
slots; do NOT add a test file (~1.4 s of build cost forever per file, ~ms per
slot).

- [x] 1.1 Batch: POST an array of two `id`-bearing requests, assert a JSON array of two responses with matching `id`s
- [x] 1.2 Batch: POST an array of notifications only, assert HTTP 202 with an empty body
- [x] 1.3 Terminated session: `DELETE` a session, then POST with that ID, assert HTTP 404
- [x] 1.4 Unrecognized session: POST with a never-issued ID, assert it is still served (pins the interop path the 404 must not eat — this one PASSES today and must keep passing)
- [x] 1.5 Resource contents: read a resource at `2025-11-25`, assert no `structuredContent` key in any `contents[]` entry and that `text` still parses to the payload
- [x] 1.6 Resource not found: `resources/read` an unknown URI, assert `code: -32002` and `data.uri`
- [x] 1.7 Unknown tool: `tools/call` an unregistered name, assert `code: -32602`
- [x] 1.8 SSE priming: open a stream, assert the first event carries an `id` and empty `data`, and that a `retry` field is present

## 2. Batch requests (MUST — 2024-11-05, 2025-03-26)

- [x] 2.1 In `handleHttpRequest`, branch on `doc.isArray()` before the object path. NOT byte-identical as written here: the session resolution both paths need was extracted into `resolveSessionForMessage()` rather than duplicated, per the centralization rule. Behaviour of a single-message POST is unchanged, and `singleObjectPostStillAnswersWithSingleObject` pins that
- [x] 2.2 Process elements sequentially through the existing per-message logic; collect responses for `id`-bearing elements only
- [x] 2.3 An element returning `_deferred` gets a JSON-RPC error in its slot naming the reason (see design). Log it — a batched confirmation request is a client doing something no client here has done, and the log is how we find out it happened
- [x] 2.4 All-notification batch → HTTP 202, no body, matching the single-notification case at `:541`
- [x] 2.5 Empty array → JSON-RPC `-32600` Invalid Request per JSON-RPC 2.0

## 3. Terminated-session 404 (MUST)

- [x] 3.1 Add a bounded tombstone set of terminated session IDs to `McpServer` (cap + oldest-first eviction; state the cap and why at the declaration)
- [x] 3.2 Record on `DELETE` (`src/mcp/mcpserver.cpp:629`) and in the expiry reaper (`cleanupExpiredSessions`)
- [x] 3.3 In the non-`initialize` session lookup (`:456`), 404 a tombstoned ID BEFORE the auto-recovery branch at `:467` — and leave that branch reachable for unrecognized IDs
- [x] 3.4 `initialize` carrying a tombstoned ID still creates a fresh session (it is the documented recovery move; 404ing it would strand the client)
- [x] 3.5 Verify the `_deferred`/pending-confirmation cleanup on `DELETE` (`:634`) still runs before the tombstone is recorded

## 4. Resource contents (schema conformance)

- [x] 4.1 Grep for readers of `structuredContent` inside `contents[]` — the tree, ShotServer JS, and the AI/MCP client code — before removing. Expected: none
- [x] 4.2 Remove `content["structuredContent"] = resourceData;` from both the async and sync paths (`src/mcp/mcpserver.cpp:944`, `:983`) and the now-dead `emitStructured` computation in `handleResourcesRead`
- [x] 4.3 Delete the test asserting `resources/read` omits `structuredContent` at 2024-11-05 (`tests/tst_mcpserver_protocol.cpp:705`). It does not over-specify — it pins a field the schema has never had, so leaving it would make the removal read as a regression
- [x] 4.4 Confirm `tools/call` `structuredContent` is untouched: that one IS in the schema and IS version-gated correctly

## 5. Error codes

- [x] 5.1 `-32002` + `data.uri` for a resource the registry does not serve (`src/mcp/mcpserver.cpp:952`, `:969`). Distinguish not-found from other read failures rather than mapping every failure to the new code
- [x] 5.2 `-32602` for an unregistered tool (`:868`); leave "Tool is async" and "Access level insufficient" at `-32603` and say why at the site
- [x] 5.3 They were NOT distinguishable — picking a code would have meant matching `"Unknown tool: "` against the message. Added `McpRegistryFailure` as an optional out-param on `callTool`/`callAsyncTool`/`readResource`/`readAsyncResource`; existing callers are unaffected

## 6. SSE priming (SHOULD — 2025-11-25)

- [x] 6.1 Emit `retry:` and a priming event (`id:` + empty `data:`) immediately after the SSE headers (`src/mcp/mcpserver.cpp:607`)
- [x] 6.2 Per-session monotonic event-ID counter; every pushed event carries an `id` (`sendSseNotification`, `:228`)
- [x] 6.3 Pick the `retry` interval against the existing 30 s keepalive probe so the two don't fight; state the reasoning in a comment
- [x] 6.4 Do NOT implement `Last-Event-ID` replay — out of scope per the design, and a partial implementation is worse than none

## 7. Verify

- [x] 7.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [x] 7.2 Break each fix in turn and confirm its section-1 test goes red. Six fixes, six red proofs
- [ ] 7.3 **NOT DONE — the one verification gap in this change.** Live `mcp-remote` / Claude Desktop / cloud-connector matrix against an EXPIRED session. The design's mitigation is in place (only IDs the server itself ended are tombstoned; unrecognized IDs keep the auto-recovery path, and `initialize` is always accepted), and `unrecognizedSessionIsStillServed` pins it, but that is an argument, not a live client. Called out in the PR body
- [ ] 7.4 Contingent on 7.3
- [ ] 7.5 Not run — the protocol tests drive the same `handleHttpRequest` entry point over a real socket pair, which is what the curl probe was standing in for
- [ ] 7.6 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 8. Document

- [x] 8.1 `docs/CLAUDE_MD/MCP_SERVER.md`: resource-response shape (no `structuredContent` in `contents[]`, and why), the two error codes, batch acceptance, and what a 404 means to a client
- [x] 8.2 Record the ONE deliberate non-conformance in the same place: the server binds all interfaces rather than localhost, because ShotServer exists to be LAN-reachable, mitigated by the `Origin` allowlist and the capability-URL gate. Written down so the next audit does not re-litigate it
- [ ] 8.3 NOT done — landed as one commit alongside the outcome-reporting change, at the maintainer's request to ship both as a single PR. The client-breaking item (the 404) is revertable on its own as a code change; it is `resolveSessionForMessage`'s tombstone branch plus `recordTerminatedSession`
- [ ] 8.4 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 8.5 Archive the change + spec sync as the final commit on the same PR
