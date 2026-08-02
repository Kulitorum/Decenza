## 1. Establish the current behaviour with a failing test

- [ ] 1.1 Add a slot to `tests/tst_mcpserver_protocol.cpp` (NOT a new file) that calls a tool whose result carries `error` and asserts the response carries `isError: true`. Confirm it FAILS against the current code before writing the fix — this is the whole premise of the change and it should be demonstrated, not assumed
- [ ] 1.2 Add the companion assertion: a successful tool call carries no `isError` key at all (sparse-emit, never `isError: false`)
- [ ] 1.3 Assert the error TEXT is still readable in `content` on a failed call — the fix must not trade one loss for another

## 2. Fix it centrally

- [ ] 2.1 In `McpServer::buildToolCallResponse` (`src/mcp/mcpserver.cpp:303`), detect an `error` key on the incoming tool payload and set `isError: true` on the returned envelope. Leave the error text in `content`
- [ ] 2.2 Remove the hand-rolled `result["isError"] = true` from the confirmation-denial path (`:1189`) now that the shared path covers it. Verify the denial response is unchanged by the swap
- [ ] 2.3 Comment `sendJsonRpcResponse`'s `contains("error")` branch (`:1346`): correct for plain JSON-RPC methods, cannot fire for tool calls because the payload is wrapped first, and MUST NOT be "fixed" by unwrapping — a tool failure is a JSON-RPC `result`, not a JSON-RPC `error`
- [ ] 2.4 Confirm the async path (`sendAsyncToolResponse`, `:1246`) routes through `buildToolCallResponse` and so inherits the behaviour; if it does not, fix that rather than duplicating the rule

## 3. Check the assumption the fix rests on

- [ ] 3.1 Grep all `error` keys returned by tools in `src/mcp/` and confirm none returns `error` as ordinary DATA rather than as a failure (the design's first open question). If any does, fix that tool's payload — do not weaken the rule
- [ ] 3.2 Spot-check that no tool relies on a failure currently being reported as success (e.g. a client-side retry that would now see a failure). Expected: none

## 4. Verify

- [ ] 4.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before building, Qt Creator is shared
- [ ] 4.2 Break `buildToolCallResponse`'s new branch and confirm the tests from section 1 go red. A test that cannot fail is a comment that compiles
- [ ] 4.3 Exercise a real failing tool end-to-end over MCP (e.g. point `shots_list` at an unopenable database) and confirm the client sees a failed tool call rather than an empty success
- [ ] 4.4 Read the `text-invariants.yml` PR run before merging — it gates `src/**` and nothing blocks a merge on it

## 5. Document

- [ ] 5.1 Add the convention to the data-conventions section of `docs/CLAUDE_MD/MCP_SERVER.md`: a tool reports failure by returning an `error` key, the server marks the call failed, and `error` is therefore a RESERVED key in a tool payload
- [ ] 5.2 Note in the same place that tool failures stay JSON-RPC `result`s, so nobody reaches for `sendJsonRpcError` from a tool
- [ ] 5.3 Open the PR, then run the automated `/pr-review-toolkit:review-pr` before merging
- [ ] 5.4 Archive the change + spec sync as the final commit on the same PR
