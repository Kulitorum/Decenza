## 1. Drop inline tool icons (standalone, ~216 KB)

- [x] 1.1 Remove the `emitIcons` branch from `McpToolRegistry::listTools()`; keep `iconsArrayFromQrc` for resource listings and delete `iconQrcForTool` if nothing else calls it
- [x] 1.2 Update `tst_mcpserver_protocol.cpp`: tool records assert NO `icons` key and no `data:` substring; resource records still assert one
- [x] 1.3 Measure and record the before/after `tools/list` byte size in the change's notes (a test that prints the size is fine; the number goes in the PR body)

## 2. Registry: tiers, ordering, action metadata

- [x] 2.1 Add `tier` (0 core / 1 standard / 2 niche) to `McpToolDefinition`, defaulting to 1, and sort `listTools()` by `(tier, name)`
- [x] 2.2 Add an action list to `McpToolDefinition` — each action declaring `name`, `category`, `confirm`, and a confirmation description — plus a `registerActionTool()` overload
- [x] 2.3 Add `McpToolRegistry::categoryFor(tool, args)` and `confirmationFor(tool, args)`, both failing closed to the most restrictive declared action when `action` is absent or unrecognised
- [x] 2.4 Assign tiers to the existing tools (core: machine, shots, dialing, profiles, recipe, scale; niche: mqtt, apply_theme, backup_now, ai conversations, debug)
- [x] 2.5 Test: `tools/list` order is stable across two registry builds and tiers sort as declared

## 3. Server: action-aware gating

- [x] 3.1 Change `needsChatConfirmation`, `needsInAppConfirmation`, and `confirmationDescription` to take `(toolName, arguments)` and delegate to the registry, keeping the hard-coded name list as the fallback for unmerged tools
- [x] 3.2 Change the rate-limit / control-counter lookup in `handleToolsCall` to use `categoryFor(toolName, arguments)`
- [x] 3.3 Emit `<tool>.<action>` in the confirmation payload's `action` field and in `confirmationRequested`
- [x] 3.4 Verify handlers still never see `confirmed` (the strip happens before dispatch) and add a test asserting it for a merged tool
- [x] 3.5 Tests: read action not rate-limited, destructive action still confirms, missing `action` fails closed on both confirmation and access level

## 4. Merge the tool families

- [x] 4.1 `machine_start` (espresso, steam, hot_water, flush) — carries the per-operation confirmation descriptions
- [x] 4.2 `scale_timer` (start, stop, reset)
- [x] 4.3 `steam_pitcher` and `water_vessel` (add, delete, list, select, update)
- [x] 4.4 `bag` (create, list, select, update); `bag_extract_details` stays separate
- [x] 4.5 `flow_calibration` (get, set, clear)
- [x] 4.6 `auto_load` (get, set, clear × target profile|recipe), including the mutual-exclusivity rule in one place
- [x] 4.7 `equipment` (list, select, update, merge)
- [x] 4.8 `mqtt` (connect, disconnect, publish_discovery), `devices_wifi` (browse, results), `ai_conversations` (list, get)
- [x] 4.9 `reset_saw_learning` (optional `profileFilename` replaces `reset_saw_learning_for_profile`)
- [x] 4.10 Update `tst_mcptools_presets.cpp`, `tst_mcptools_write.cpp`, `tst_mcptools_profiles.cpp`, `tst_mcpserver_protocol.cpp` to the merged names
- [x] 4.11 Confirm the count: 97 → 66 registered tools (65 in `mcptools_*.cpp` plus `debug_get_log`, which lives in `mcpresources.cpp`)

## 5. Move long-form documentation off the wire

- [x] 5.1 Add `resources/ai/tools/<topic>.md` sources for the tools whose descriptions exceed the budget (`dialing_get_context`, `dialing_get_grinder_calibration`, `ai_advisor_invoke`, `flow_calibration`, `equipment`, `bag`, `devices_set_scale_priority_mode`, `shots_get_debug_log`)
- [x] 5.2 Add the optional `topic` argument to `get_agent_file`, with an unknown-topic error that lists the topics
- [x] 5.3 Register the same content as `decenza://tools/<topic>` resources
- [x] 5.4 Rewrite the over-budget tool descriptions down to selection-and-arguments only, each ending with a pointer to its topic
- [x] 5.5 Rewrite schema property descriptions over 120 characters
- [x] 5.6 Tests: topic returns markdown, unknown topic errors, no-argument call is unchanged

## 6. Budget gate

- [x] 6.1 Write `scripts/check_mcp_tool_budget.py` — parses `registerTool`/`registerAsyncTool` in `src/mcp/mcptools_*.cpp`, enforces the four limits plus the no-inline-`data:` rule, and names the offending tool and limit on failure
- [x] 6.2 Wire it into `.github/workflows/text-invariants.yml` as a blocking step, with a header comment explaining what it protects (matching the file's existing style)
- [x] 6.3 Verify it fails on a deliberately over-long description and passes on the merged tree

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/MCP_SERVER.md`: merged tool list, the action convention, the budget and where its numbers live, and the docs-topic channel
- [x] 7.2 Update `CLAUDE.md`'s MCP section with the rule for adding a tool (declare actions and tier; stay inside the budget; long-form prose goes to a topic)
- [x] 7.3 Check `resources/ai/claude_agent.md` for renamed tools — it names only `get_agent_file`, so no edit was needed
- [x] 7.4 No wiki manual entry — the MCP tool surface is not a user-discoverable app feature

## 8. Verify and measure

- [x] 8.1 Run the full test suite through the Qt Creator MCP (ask first)
- [x] 8.2 Measured live against the running macOS build: `tools/list` is 68.4 KB at Full access (77.0 KB with the DISABLED prefixes this session's level adds), 66 tools, no `icons` key, zero `data:` URIs, order `(tier, name)`. Against ~312 KB before — 216 KB of that icons — a 4.6x cut. Still to do: install on the tablet and confirm `[MCP][Server] Registered 66 tools`
- [ ] 8.3 Reconnect ChatGPT and count the exposed tools: 66 means the surface now fits; fewer means the client's cap is lower than assumed, and the tier order says exactly what it dropped
- [ ] 8.4 Record the answer (count cap vs byte budget) in the change notes, and open a follow-up for the high-traffic families (`profiles_*`, `recipe_*`, `devices_*`, `shots_*`) only if step 8.3 still shows truncation
- [ ] 8.5 Open the PR, run `/pr-review-toolkit:review-pr`, then archive + spec-sync as the final commit on the same PR
