## Context

`McpToolRegistry` holds every tool in a `QHash<QString, McpToolDefinition>` and `listTools()` walks it in hash order, emitting name, title, description, input schema, and (on 2025-11-25) icons for all 97 registrations. Nothing bounds that payload. Measured over the current tree:

| Component | Bytes | Share |
|---|---|---|
| Inline base64 icons | ~216 KB | 87% |
| Tool descriptions | ~32 KB | 13% |
| Schema property descriptions, titles, names | (inside the schemas) | — |

The icons are the surprise and they are almost pure duplication. `iconQrcForTool()` maps a tool's *name prefix* to an SVG and falls back to `decent-de1.svg` on a miss; the prefixes it knows are `machine`, `shots`, `profiles`, `settings`, `scale`, `steam`, `devices`, `agent`, `dialing`, `debug`, so 41 of 97 tools — every `recipe_*`, `bag_*`, `equipment_*`, `water_vessel_*`, `mqtt_*`, `ai_*`, `get_*`, `set_*`, `clear_*` — carry the same 2292-byte file, base64'd to ~3 KB, on every single listing.

ChatGPT exposes 87 of those 97. The app is not filtering — access level never removes a tool from the listing, it only prefixes the description with `[DISABLED — requires …]` (`mcptoolregistry.h:157`), and all 97 register unconditionally (`mcpserver.cpp:161`). The truncation is client-side. Because the emission order is hash order, the ten that disappear are effectively arbitrary and can change when the app restarts, which is how `clear_flow_calibration` stayed while `get_flow_calibration` and `set_flow_calibration` vanished.

Three server behaviours currently key on tool *name* and constrain any merge:

- `needsChatConfirmation()` / `needsInAppConfirmation()` / `confirmationDescription()` — a hard-coded name list at `mcpserver.cpp:1715`. Confirmation is enforced server-side on purpose; a handler that checks `confirmed` itself is unreachable-true, which was the shipped #1219 bug.
- Rate limiting and the control-call counter, which look up `toolCategory(toolName)` in `handleToolsCall`.
- Access level, also from the tool's single category.

A merged tool has one name and several verbs with different answers to all three.

## Goals / Non-Goals

**Goals:**
- Get the whole tool surface inside what real clients will accept, and keep it there as tools are added.
- Make client truncation deterministic and least-harmful when it still happens.
- Preserve every capability: no tool's function is removed, only its address changes.
- Keep confirmation, rate limiting, and access level exactly as strict per action as they are per tool today.

**Non-Goals:**
- Not a redesign of what the tools do, their arguments, or their return shapes beyond the added `action`.
- Not merging the high-traffic families (`profiles_*`, `recipe_*`, `shots_*`, `devices_connect*`) in this change — see the Migration Plan.
- Not `tools/list` pagination. The server does not paginate today and adding a cursor does not help: a client that truncates a single page is not asking for a second one.
- Not a deprecation window for old names. MCP clients re-read the tool list on every connection.

## Decisions

### 0. Drop inline icons from `tools/list`

`icons` is an optional field whose only consumer is a client UI that wants a picture next to a tool name. It costs 87% of the payload, and 41 of the 97 pictures are the same generic fallback that identifies nothing. Tools stop emitting it. Resource listings keep theirs — five resources, ~10 KB, and each one is a distinct icon.

Considered: keeping icons but serving `src` as an `https://…/icons/<name>.svg` URL against the app's own HTTP server, which MCP permits and which would cost ~40 bytes per tool instead of ~3 KB. Rejected for now — it means a new unauthenticated GET route with its own Origin validation, and it has to be reachable through the remote-access tunnel for remote clients, all to restore a cosmetic field nobody asked for. If a client ever renders tool icons usefully, that is the way to bring them back, and dropping them now does not foreclose it.

This decision is independent of every other one here: it needs no tool renamed and no spec changed, and it is where nearly all the bytes are.

### 1. Merge by noun, with the old suffix as a required `action` value

A merged tool is named for its noun (`steam_pitcher`, `water_vessel`, `bag`, `flow_calibration`, `auto_load`, `equipment`, `mqtt`, `machine_start`, `scale_timer`, `devices_wifi`, `ai_conversations`) and takes `action` as a **required** enum whose values are the old name suffixes (`list`, `add`, `select`, `update`, `delete`, …). Reusing the suffix verbatim means a model that learned `steam_pitcher_select` transfers to `steam_pitcher(action:"select")` without being told.

`action` is required and has no default. A default would let an omitted argument resolve to *some* behaviour, and the destructive members of these families (`delete`, `clear`, `set`) are exactly where that is worst.

Considered and rejected: keeping every name and only trimming descriptions. It helps only if the client's limit is bytes; if it is a count, it does nothing, and the count grows with every feature either way. Also rejected: one mega-tool per domain (`data(action:…)` over profiles + recipes + shots), which pushes the model's selection problem from a name it is good at into a 20-value enum it is worse at.

**First-pass merges (97 → 67 tools):**

| Merged tool | Replaces | Δ |
|---|---|---|
| `machine_start` | `machine_start_{espresso,steam,hot_water,flush}` | −3 |
| `scale_timer` | `scale_timer_{start,stop,reset}` | −2 |
| `steam_pitcher` | `steam_pitcher_{add,delete,list,select,update}` | −4 |
| `water_vessel` | `water_vessel_{add,delete,list,select,update}` | −4 |
| `bag` | `bag_{create,list,select,update}` (not `bag_extract_details`) | −3 |
| `flow_calibration` | `{get,set,clear}_flow_calibration` | −2 |
| `auto_load` | `profiles_{get,set,clear}_auto_load`, `recipe_{get,set,clear}_auto_load` | −5 |
| `equipment` | `equipment_{list,select,update,merge}` | −3 |
| `mqtt` | `mqtt_{connect,disconnect,publish_discovery}` | −2 |
| `devices_wifi` | `devices_wifi_{browse,results}` | −1 |
| `ai_conversations` | `ai_conversation_get`, `ai_conversations_list` | −1 |
| `reset_saw_learning` | `reset_saw_learning`, `reset_saw_learning_for_profile` | −1 |

`auto_load` additionally takes `target: "profile" | "recipe"`, which is where the mutual exclusivity these two groups already enforce becomes visible in one schema instead of being described six times in prose.

`bag_extract_details` stays separate: it is a different operation (parse a photographed bag label) that happens to share a noun.

### 2. Per-action metadata, resolved server-side

`McpToolDefinition` gains a list of actions, each declaring its own `category` (which drives access level and rate limiting) and, where needed, `confirm` plus a confirmation description. The registry answers `categoryFor(tool, args)` and `confirmationFor(tool, args)`; `handleToolsCall` calls those instead of the name-keyed lookups, and the hard-coded name list at `mcpserver.cpp:1715` becomes a fallback for unmerged tools only.

Gating **fails closed**: when `action` is missing or not a declared value, the server resolves category and confirmation to the *most restrictive* action the tool declares. The handler still returns the "unknown action" validation error, but it does so after the same gate a destructive action would have faced, so an omitted argument can never be a way past confirmation.

The confirmation payload's `action` field carries `tool.action` (e.g. `steam_pitcher.delete`) rather than the bare tool name, so the in-app dialog and the chat confirmation both say which verb is being confirmed.

Handlers still never inspect `confirmed` — the server strips it before dispatch, unchanged.

### 3. Long-form documentation moves behind `get_agent_file`

Tool descriptions keep only what a client needs to *select* the tool and fill its arguments. Everything else — output-field meanings, worked sequences, the interaction rules that make several of today's descriptions 700–2300 characters — moves into markdown served by `get_agent_file`, which gains an optional `topic` argument. The same content is also registered as `decenza://tools/<topic>` resources for clients that prefer resources.

`get_agent_file` already exists, already returns versioned markdown, and is already advertised as the thing to read at session start, so this costs no tool slot. A dedicated `help` tool was considered and rejected for that reason; a resource-only channel was rejected because the client that is truncating today ignores resources.

The descriptions that shrink most are the ones documenting *output* (`dialing_get_context` at 2289 chars, `dialing_get_grinder_calibration` at 1487, `get_flow_calibration` at 751). Output shape is exactly what a client does not need before choosing a tool.

### 4. Explicit budget, enforced by a build-free check

`scripts/check_mcp_tool_budget.py` parses the `registerTool`/`registerAsyncTool` calls in `src/mcp/mcptools_*.cpp` and fails on: more than **80** tools, any tool description over **500** characters, any schema property description over **120** characters, an estimated `tools/list` payload over **45 KB**, or any inline `data:` URI reachable from a tool listing. It runs in `text-invariants.yml`, which already gates `src/**`, needs no build, and takes under a second — the shape a new PR-time check has to fit.

The inline-`data:` rule is the one that would have caught the icons: nothing about 216 KB of base64 was visible in review, because the code that produced it is one call to a helper.

These four numbers are one edit in one file. They are an initial setting, not a measurement of the client's real limit, which is why the change includes an empirical step (below) to learn whether that limit is a count or a byte budget.

### 5. Deterministic, importance-ordered listing

Each tool declares a tier: `0` core (machine control and state, shots, dialing, profiles, recipes, scale), `1` standard, `2` niche (mqtt, theme, backup, ai conversations, debug). `listTools()` emits sorted by `(tier, name)`. A client that truncates then always loses the same tail, and that tail is the least useful part of the surface.

Sorting also removes a second-order problem: `QHash` iteration order is not stable across runs, so today two sessions of the same build can present tools in different orders, which makes any client-side truncation unreproducible.

## Risks / Trade-offs

- **Enum dispatch is harder for a model than distinct tool names** → keep the highest-traffic families unmerged, reuse the old suffix as the action value so prior knowledge transfers, and make the unknown-action error enumerate the valid values.
- **A merged tool's description must cover several actions, pushing against the 500-char budget** → the budget is per tool, and the freed space comes from moving output documentation out entirely; if a merged tool cannot be described in 500 characters, that is the signal it merged too much.
- **Fail-closed gating prompts for confirmation on a malformed call that would only have errored** → accepted. An extra prompt on a malformed call is cheaper than a missed prompt on a destructive one.
- **Some clients never read `get_agent_file`, so the moved documentation is invisible to them** → descriptions stay self-sufficient for selection and argument-filling; what moves is depth, not the contract.
- **The budget numbers may not match the real client limit** → the change ends with a measurement, and the numbers live in one file so tightening them is a one-line edit.
- **Specs and tests name the old tools** → `mcp-server`, `profile-auto-load`, and `grind-rpm-pairing` carry delta specs in this change, and the four MCP test files are updated with it.
- **`resources/ai/claude_agent.md` names tools** → updated in the same change; it is versioned with the app and clients re-read it per session.

## Migration Plan

0. Drop tool icons. Standalone, ~216 KB, no renames — land it first so the rest is measured against a realistic payload.
1. Registry gains action metadata, tiers, and sorted listing; server gating becomes action-aware. No tool names change yet, so the suite stays green on the old names.
2. Merge the twelve families, updating specs, tests, `claude_agent.md`, and `docs/CLAUDE_MD/MCP_SERVER.md`.
3. Move long-form prose into the docs topics; add the budget check and turn it on.
4. **Measure**: install the build on the tablet, reconnect ChatGPT, count exposed tools. 67 of 67 exposed means the limit was the count or was cleared by the byte reduction. Anything less than 67 means the client's cap is lower than assumed, and the tier ordering tells us exactly what it dropped.
5. Only if step 4 still shows truncation: a follow-up change merges the high-traffic families (`profiles_*` 13, `recipe_*` 11, `devices_*` 10, `shots_*` 7), which is where the remaining count lives.

Rollback is a revert; there is no persisted state and no client migration to unwind.

## Open Questions

- Is the client's limit a tool count or a payload size? Step 4 answers it; the design does not depend on the answer, only the budget numbers do.
- Should `scale_tare` and `scale_get_weight` merge into a `scale` tool? They are two of the most-called tools and merging saves one slot, so it is deferred to the follow-up.
