## 1. Narrow the advertised set

- [ ] 1.1 Remove `2024-11-05` and `2025-03-26` from `supportedProtocolVersions()`. Leaves `2025-11-25` (preferred, first) and `2025-06-18`
- [ ] 1.2 Do NOT remove `2025-06-18`. `codex-mcp-client` negotiates it — the one drop with a known victim. The reason is a measurement, not a guess; see design.md
- [ ] 1.3 Test: a client requesting a dropped revision is answered with the preferred version, which is the existing unsupported-version behaviour and needs no new code

## 2. Delete batch dispatch

- [ ] 2.1 Delete `handleJsonRpcBatch` (136 lines) and `willDeferResponse` (13), and their declarations
- [ ] 2.2 Replace the `doc.isArray()` branch in `handleHttpRequest` with an explicit JSON-RPC error naming batching as unsupported. Do not silently ignore an array — a client that sends one deserves to be told, and a 202 or a parse error would both mislead
- [ ] 2.3 Remove the batch coverage in `tst_mcpserver_protocol.cpp` (36 references). Replace with ONE test that an array body is refused with an error rather than processed
- [ ] 2.4 Check nothing else calls either function before deleting — `willDeferResponse` exists only for the batch path, but confirm rather than assume

## 3. Collapse the gates whose old arm is unreachable

- [ ] 3.1 `instructions` in `handleInitialize`: drop the `>= 2025-03-26` gate, keep the field
- [ ] 3.2 `emitTitle` in `McpToolRegistry::listTools` and `McpResourceRegistry::listResources`: drop the `>= 2025-06-18` gate, always emit `title`
- [ ] 3.3 Leave `structuredContent` / `resource_link` (2025-06-18) and `$schema` dialect / `icons` (2025-11-25) gated. Both surviving revisions are still distinguishable by them
- [ ] 3.4 Move the version-floor constants to `2025-06-18`: `PendingConfirmation::protocolVersion`, the `effectiveProtocolVersion` fallback, and `McpSession::m_protocolVersion`
- [ ] 3.5 Fix the always-emitted text block's comment. It claims to exist for "2024-11-05 / 2025-03-26 clients", which was never why: `content[]` is required on a tool result at every revision and `structuredContent` is additive. Left as-is it reads as a licence to delete the block

## 4. Record the deviation

- [ ] 4.1 The protocol says assume `2025-03-26` when the header is absent. We cannot assume a version we refuse, so the floor becomes `2025-06-18`. Write that at the site as a knowing deviation from a SHOULD, with the reason it is the safe direction — the floor emits strictly fewer optional fields
- [ ] 4.2 `docs/CLAUDE_MD/MCP_SERVER.md`: the supported set, and that batching is gone

## 5. Verify

- [ ] 5.1 `mcp__qtcreator__run_tests` — ask first, Qt Creator is shared. Named scope is enough for the MCP tests; a full run before the PR
- [ ] 5.2 Conformance against a running app with the checked-in baseline. Expect the same green, and note the suite has no scenarios for either dropped revision — so it cannot confirm the removal either way. It CAN confirm nothing else regressed, which is the point of running it
- [ ] 5.3 Live-check over both callers, ShotServer and `McpRemoteAccess`
- [ ] 5.4 No wiki manual change — protocol revisions are not a user-visible app surface
- [ ] 5.5 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 5.6 Archive + spec sync as the final commit on the same PR

## 6. Sequencing note

- [ ] 6.1 This lands BEFORE `add-mcp-dual-era-2026`, and both modify the `MCP-Protocol-Version Request Header` requirement — this one changes the header-absent assumption, that one changes the mismatch rule. Whichever lands second rebases and merges both edits into one requirement block. Flagged here so the collision is expected rather than discovered
- [ ] 6.2 `McpSurfaceVersion` does NOT need bumping. It tracks the TOOL surface, which is unchanged; `scripts/check_mcp_tool_budget.py` fingerprints tools and actions, not protocol revisions
