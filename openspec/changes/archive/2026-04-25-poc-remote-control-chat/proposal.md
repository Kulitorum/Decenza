# Change: POC — AI Chat via Claude Code Remote Control

## Why
Before investing in a full custom bridge service, validate whether Claude Code Remote Control delivers a good enough end-to-end experience for the "dialing in a bag of coffee" use case. Remote Control ships with Claude Code, requires no custom server infrastructure, and supports the Claude iOS/Android app as a first-class client. If the POC experience is good, it replaces the bridge entirely. If not, findings inform what the bridge needs to do better.

## What This Is Not
This is **not** the full bridge implementation (`add-claude-code-mcp-chat`). That spec remains as the fallback/full-featured path. This POC deliberately avoids building a native Decenza chat UI or a custom bridge service — the goal is to test the experience with the least possible code.

This is also **not** an automated Remote Control lifecycle manager. In this POC, the user owns the `claude remote-control` process: they run it themselves on their desktop, paste the session URL into Decenza, and Decenza just opens that URL when the Discuss button is tapped. Automating the process lifecycle (spawn, restart, config write) was considered but descoped to keep the POC minimal — it would add `QProcess` and platform-specific config-file writing that would be thrown away if the POC fails.

## What Changes
- Add **Claude Desktop** as a new option in the Discuss app selector (alongside Claude App, ChatGPT, etc.) — appended at index 7 so existing users on "None" (index 6) don't need a migration
- Add a `claudeRcSessionUrl` setting that stores a session URL pasted by the user in AI settings; the Discuss button opens this URL via `Qt.openUrlExternally()` when Claude Desktop mode is selected
- Expose a **`current_dialing_context` MCP resource** (`decenza://dialing/current_context`) that Claude reads at the start of each turn — current bean, roast date, last 3 shots, active profile, grinder context, and machine phase — so the conversation is always grounded without the user having to explain context
- Add a **`get_agent_file` MCP tool** that returns the current `CLAUDE.md` content and version — Claude calls this to self-bootstrap on first connection and self-update on subsequent sessions
- Add a new **"AI Chat (Claude Code Remote Control)" section** to the existing `/mcp/setup` web page with step-by-step instructions and a copy-paste `.mcp.json` block pre-filled with the live Decenza MCP URL; also fix the existing out-of-date "Claude iOS/Android doesn't work" compatibility note
- Ship a baked-in **`CLAUDE.md` template** (as `resources/ai/claude_agent.md`) with a `{{VERSION}}` placeholder that is substituted with the current Decenza app version at tool-call time; the user's host-machine `CLAUDE.md` auto-updates from this template at every session start

## Success Criteria (POC Exit)
- Tapping Discuss opens the persistent Decenza session directly (not the Claude app home screen)
- Claude demonstrates awareness of current bean, recent shots, and active profile from the `current_dialing_context` MCP resource without the user having to explain it
- Asking Claude "set up Decenza AI chat" in a fresh working directory produces a valid `CLAUDE.md` and `dialing/` folder with no scripts
- Sessions persist across days; resuming conversation the next day restores prior context via Claude reading the bean log file
- Pulling a new `VERSION` of Decenza and restarting the Remote Control session auto-updates `CLAUDE.md` via `get_agent_file`
- The experience feels fluid enough that a typical DE1 user would use it regularly — even with the manual "paste URL once" setup step

## If POC Succeeds
Archive this change. The `add-claude-code-mcp-chat` bridge spec remains as a reference for future native UI work if demand justifies it. A follow-up could re-introduce QProcess-based automation (Step 6/7 of the original task list) if manual session setup proves to be a friction point in practice.

## If POC Falls Short
Document specific gaps (e.g. context quality, session persistence, UX friction, manual-setup friction) and use them to drive bridge requirements. The bridge spec already covers most of these.

## Impact
- Affected specs: `remote-control-chat` (new, POC-scoped)
- Affected code:
  - `src/mcp/mcpresources.cpp` — new async resource + signature change
  - `src/mcp/mcpserver.cpp` — wire up new resource and tool
  - `src/mcp/mcptools_agent.cpp` (new file) — `get_agent_file` tool
  - `src/core/settings.{h,cpp}` — new property, new constant, updated `discussShotUrl()`
  - `src/network/shotserver.cpp` — `/mcp/setup` page updates
  - `qml/pages/settings/SettingsAITab.qml` — new dropdown entry and URL paste field
  - `qml/components/layout/items/DiscussItem.qml` — gating on session URL
  - `resources/ai/claude_agent.md` (new file) — bundled CLAUDE.md template
  - `resources/ai.qrc` — register new resource file
  - `CMakeLists.txt` — add `mcptools_agent.cpp` to sources
