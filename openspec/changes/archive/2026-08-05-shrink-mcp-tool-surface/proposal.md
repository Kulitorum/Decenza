## Why

The MCP server registers 97 tools, and a real client is already silently dropping some of them: ChatGPT exposes 87 of the 97 (the app's own log says `[MCP][Server] Registered 97 tools`, and ChatGPT reports 87). The user could not call `get_flow_calibration` or `set_flow_calibration` because those two were among the ten that fell off — while `clear_flow_calibration`, from the same trio, survived.

The payload is far larger than the tool descriptions suggest. On the 2025-11-25 protocol, `listTools()` attaches a base64 `data:` URI of an SVG to every tool — **216 KB of icons against 32 KB of descriptions**, so icons are 87% of what `tools/list` sends. Most of that is one file repeated: `iconQrcForTool()` maps a tool's name prefix to an icon and falls back to `decent-de1.svg` (2292 bytes, ~3 KB base64) on a miss, and 41 of 97 tools miss — every `recipe_*`, `bag_*`, `equipment_*`, `water_vessel_*`, `mqtt_*`, and `ai_*` tool ships the same generic machine drawing, inline, on every listing.

Two more things make this worse than a one-off. First, `m_tools` is a `QHash`, so `tools/list` emits in arbitrary hash order and *which* tools a truncating client drops changes between app restarts — the failure is unreproducible and looks like a bug in the tool. Second, the tool count and the description bytes only ever go up: every feature since has added tools rather than folded them in. Whatever the client's exact limit is (count or payload bytes), the surface is already past it and growing toward the next one.

## What Changes

- Stop attaching inline base64 icons to `tools/list`. This alone removes ~216 KB — 87% of the payload — and no capability depends on it: `icons` is an optional cosmetic field for client UIs. Resource listings keep theirs (five resources, negligible).
- **BREAKING** Merge same-noun tool families into single verb-enum tools — the verb becomes an `action` argument. First pass covers `machine_start_*`, `scale_timer_*`, `steam_pitcher_*`, `water_vessel_*`, the `bag` CRUD group, `{get,set,clear}_flow_calibration`, `equipment_*`, `mqtt_*`, `devices_wifi_*`, `ai_conversation*`, and `reset_saw_learning*`. The `profiles_*`/`recipe_*` auto-load groups merge into one `auto_load` tool, which also makes their mutual exclusivity (a profile auto-load and a recipe auto-load cannot both be set) expressible in one place instead of six.
- Per-tool **description budget**: tool descriptions and schema property descriptions are capped at a fixed character count. Descriptions keep exactly what a client needs to *choose* the tool and fill its arguments.
- Long-form documentation — argument semantics, gotchas, worked sequences, output-field meanings — moves out of `tools/list` and into on-demand retrieval, so it costs context only when a client asks for it.
- `listTools()` emits in a **deterministic, importance-ordered** sequence instead of `QHash` order, so a truncating client always loses the same, least-important tail rather than a random ten.
- Confirmation gating, rate limiting, and access level become **action-aware**: they currently key on tool name alone, which a merged tool breaks (a merged `steam_pitcher` would otherwise make `list` as confirmable and as rate-limited as `delete`).
- A build-free per-PR check enforces the budget — tool count, description length, inline-icon bytes, and total `tools/list` payload — in `text-invariants.yml`, alongside the existing checks.

## Capabilities

### New Capabilities
- `mcp-tool-surface-budget`: the tool surface as a budgeted resource — a cap on tool count and on the bytes `tools/list` spends per tool, deterministic importance-ordered listing so client truncation is predictable, on-demand delivery of long-form tool documentation, and the CI check that keeps all of it from regressing as tools are added.
- `mcp-action-dispatch`: the verb-enum tool convention — how a merged tool declares its actions, and how confirmation, rate limiting, and access level resolve per action rather than per tool name.

### Modified Capabilities
- `mcp-server`: tool names change for every merged family, and the requirements naming `equipment_list`, `equipment_select`, `equipment_update`, and `devices_set_scale_priority_mode` are restated against the merged tools.
- `profile-auto-load`: the `profiles_get_auto_load` / `profiles_set_auto_load` / `profiles_clear_auto_load` requirements are restated as actions of the single `auto_load` tool.
- `grind-rpm-pairing`: the requirements naming `machine_start_espresso` and `bag_create`/`bag_update` are restated against the merged `machine_start` and `bag` tools.

## Impact

- `src/mcp/mcptoolregistry.h` — tool definition gains action metadata; `listTools()` ordering; description budget is enforced at registration in debug builds.
- `src/mcp/mcpserver.cpp` — `needsChatConfirmation`, `needsInAppConfirmation`, `confirmationDescription`, and the rate-limit/category lookup in `handleToolsCall` all take arguments, not just a tool name.
- `src/mcp/mcptools_*.cpp` — every merged family's registration is rewritten; long-form prose moves to the docs resource.
- `resources/ai/` — new long-form tool documentation source.
- `scripts/check_mcp_tool_budget.py` + `.github/workflows/text-invariants.yml` — new blocking check.
- `tests/tst_mcpserver_protocol.cpp`, `tst_mcptools_presets.cpp`, `tst_mcptools_write.cpp`, `tst_mcptools_profiles.cpp` — tool names and confirmation expectations.
- `docs/CLAUDE_MD/MCP_SERVER.md` and `CLAUDE.md` — the tool list and the rule for adding a tool.
- External MCP clients re-read the tool list on every connection, so renames need no deprecation window; `resources/ai/claude_agent.md` and any saved client prompts that name tools do need updating.
