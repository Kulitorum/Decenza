## 0. Approval Gate
- [x] 0.1 Review and approve this POC proposal before implementation
- [x] 0.2 ~~Verify stdin `/rename`~~ — **does not work in server mode; session name fixed at launch as "Decenza_REMOTE"**
- [x] 0.3 ~~Verify iOS connection~~ — **confirmed working**
- [ ] 0.4 Verify: what does `claude remote-control` print to stdout — identify the line/format containing the session URL
      *(Deferred to POC evaluation — user copies the URL from their own terminal, so format parsing is not required by Decenza)*
- [ ] 0.5 Verify: does the session URL deep-link into the Claude iOS/Android app directly, or browser only?
      *(Deferred to POC evaluation §9.1)*
- [ ] 0.6 Verify: does session history survive after the local process is killed (machine reboot)?
      *(Deferred to POC evaluation §9.4)*

## 1. MCP — current_dialing_context Resource
- [x] 1.1 Add `decenza://dialing/current_context` async resource to Decenza's MCP server (`src/mcp/mcpresources.cpp`)
- [x] 1.2 Resource returns: current bean (brand, type, roast date, dose), grinder (brand, model, setting), last 3 shots (id, timestamp, profile, dose, yield, duration, TDS, EY), active profile (name, editor type), machine phase
- [x] 1.3 Extend `registerMcpResources()` signature to take `Settings*`; update call site in `src/mcp/mcpserver.cpp`
- [ ] 1.4 Verify Claude reads and uses this resource correctly in a test conversation *(POC evaluation)*

## 2. MCP — get_agent_file Tool
- [x] 2.1 Add `get_agent_file` MCP tool in new file `src/mcp/mcptools_agent.cpp`, returning current `CLAUDE.md` content and a version string tied to `VERSION_STRING`
- [x] 2.2 Content is baked into the Decenza binary (loaded from `:/ai/claude_agent.md` Qt resource)
- [x] 2.3 `{{VERSION}}` placeholder in the template is substituted with `VERSION_STRING` at tool-call time
- [x] 2.4 Register via `registerAgentTools()` in `src/mcp/mcpserver.cpp`

## 3. CLAUDE.md Content
- [x] 3.1 Write `resources/ai/claude_agent.md` with instructions for Claude to:
  - Call `get_agent_file` at session start; overwrite `CLAUDE.md` if version is newer
  - Call `current_dialing_context` to identify the active bean
  - Read `dialing/{beanBrand} {beanType}.md` for prior history
  - Reference the log during conversation
  - Append a summary to the log after each discussion
- [x] 3.2 Include a version header in `CLAUDE.md` (`<!-- decenza-agent-version: {{VERSION}} -->`)
- [x] 3.3 Register `ai/claude_agent.md` in `resources/ai.qrc`

## 4. Setup Page Update
- [x] 4.1 Add new "AI Chat (Claude Code Remote Control)" section at the bottom of `/mcp/setup` with step-by-step instructions: install Claude Code, create working directory, create `.mcp.json` (copy-paste block), run `claude` once to accept workspace trust + MCP approval prompts, run `claude remote-control --name "Decenza_REMOTE" --spawn=session`, paste URL into Decenza Settings, ask Claude to "Set up Decenza AI chat"
- [x] 4.2 Fix Platform Compatibility table: remove "Does NOT work with Claude iOS/Android apps" — replace with note that those clients work via Claude Code Remote Control
- [x] 4.3 Update Discuss tip text to mention Claude Code Remote Control as a Discuss target
- [x] 4.4 Inject live Decenza MCP URL into the `.mcp.json` payload via the existing JS block

## 5. Settings — Claude Desktop Mode
- [x] 5.1 Append "Claude Desktop" as a new `discussShotApp` option at index 7 (no migration; `None` stays at 6)
- [x] 5.2 Add `claudeRcSessionUrl` setting (`Q_PROPERTY`, getter/setter, signal, `ai/claudeRcSessionUrl` QSettings key)
- [x] 5.3 Add `discussAppClaudeDesktop()` constant (returns 7) alongside existing `discussAppNone()`
- [x] 5.4 Update `discussShotUrl()` to return `claudeRcSessionUrl` when `discussShotApp == 7`
- [x] 5.5 Add Claude Desktop section to `SettingsAITab.qml`: help text + URL paste field visible only when Claude Desktop is selected

## 6. Process Management — DESCOPED
The user owns the `claude remote-control` process. Decenza does not spawn, monitor, or restart it.
- [x] 6.1 ~~Add `ClaudeRemoteControlManager` class~~ — **not built**
- [x] 6.2 ~~Launch `claude remote-control --name "Decenza_REMOTE"` on first use; restart on unexpected exit~~ — **user launches manually from terminal**
- [x] 6.3 ~~Parse session URL from stdout~~ — **user copies URL by eye from their terminal**
- [x] 6.4 ~~Do not kill subprocess on Decenza quit~~ — **N/A, no subprocess**

## 7. MCP Config Auto-Write — DESCOPED
The user creates `.mcp.json` manually from the copy-paste block on `/mcp/setup`. Claude Code auto-discovers `.mcp.json` in the current working directory.
- [x] 7.1 ~~Locate Claude Code MCP config path on Mac/Linux and Windows~~ — **not needed; `.mcp.json` is per-working-directory, not global**
- [x] 7.2 ~~Read existing config, inject Decenza MCP server entry, write back~~ — **user creates the file manually**
- [x] 7.3 ~~Skip write if Decenza entry already present~~ — **N/A**
- [x] 7.4 ~~Surface config write status in settings UI~~ — **N/A**

## 8. Discuss Button Integration
- [x] 8.1 Verify existing `DiscussItem.qml` `openDiscuss()` path works unchanged with the stored session URL — uses `Settings.discussShotUrl()` which now returns `claudeRcSessionUrl` for index 7
- [x] 8.2 Replace literal `!== 6` visibility check with `Settings.discussAppNone` constant reference
- [x] 8.3 Disable (but keep visible) the Discuss button when Claude Desktop mode is selected and `claudeRcSessionUrl` is empty, via new `isClaudeDesktopReady` property

## 9. POC Evaluation
- [ ] 9.1 Test end-to-end: tapping Discuss lands in the Decenza session (not Claude home)
- [ ] 9.2 ~~Test bean rename: change bean in settings, verify session title updates in Claude app~~ — **N/A, session name is fixed at "Decenza_REMOTE" — bean context comes from `current_dialing_context` MCP resource**
- [ ] 9.3 Test MCP context: does Claude reference current bean/shots without prompting?
- [ ] 9.4 Test persistence: close and reopen Decenza, verify session URL is restored and Discuss still works
- [ ] 9.5 Test multi-day: resume a conversation started the previous day, verify Claude reads the bean log and restores context
- [ ] 9.6 Test self-bootstrap: fresh working directory, say "Set up Decenza AI chat", verify Claude writes `CLAUDE.md` and creates `dialing/`
- [ ] 9.7 Test self-update: bump `VERSION` in `CMakeLists.txt`, rebuild, start a new session in the same CWD, verify `CLAUDE.md` is overwritten with the new version
- [ ] 9.8 Record pass/fail against success criteria in proposal.md

## 10. POC Exit Decision
- [ ] 10.1 If POC passes: archive this change, park `add-claude-code-mcp-chat` as a future enhancement
- [ ] 10.2 If POC fails: document gaps and use them to update requirements in `add-claude-code-mcp-chat`

## Build/Wire-up
- [x] Add `src/mcp/mcptools_agent.cpp` to `CMakeLists.txt` sources list
- [x] Add `ai/claude_agent.md` to `resources/ai.qrc`
