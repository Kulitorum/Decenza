# remote-control-chat Specification

## Purpose
TBD - created by archiving change poc-remote-control-chat. Update Purpose after archive.
## Requirements
### Requirement: Claude Desktop Discuss Mode
The app SHALL provide a "Claude Desktop" option in the Discuss app selector that opens a user-managed Claude Code Remote Control session via a stored session URL.

#### Scenario: User selects Claude Desktop mode
- **WHEN** the user selects "Claude Desktop" in AI Settings → Discuss app
- **THEN** Decenza reveals a text field for pasting the session URL printed by `claude remote-control`
- **AND** a link or reference to `/mcp/setup` is shown for step-by-step instructions

#### Scenario: User taps Discuss in Claude Desktop mode with a stored URL
- **WHEN** Claude Desktop mode is configured with a non-empty `claudeRcSessionUrl` and the user taps the Discuss button
- **THEN** Decenza opens the stored session URL via `Settings.openDiscussUrl()`
- **AND** on iOS, the URL opens in an in-app Safari view (`SFSafariViewController`) to avoid universal-link interception on older iPads; on all other platforms, it opens via the system URL handler which deep-links into whichever Claude client is installed

#### Scenario: User taps Discuss in Claude Desktop mode without a URL
- **WHEN** Claude Desktop mode is selected but `claudeRcSessionUrl` is empty
- **THEN** the Discuss button is disabled but remains visible
- **AND** the tap performs no action

### Requirement: Setup Page Remote Control Instructions
The `/mcp/setup` web page SHALL provide copy-paste instructions for setting up a `claude remote-control` session that connects to Decenza's MCP server.

#### Scenario: User visits /mcp/setup
- **WHEN** the user opens the MCP setup page
- **THEN** a section titled "AI Chat (Claude Code Remote Control)" explains the end-to-end setup
- **AND** a copy-paste block provides the `.mcp.json` contents with the correct Decenza MCP URL pre-filled for the requesting host
- **AND** the page instructs the user to start `claude remote-control --name "Decenza_REMOTE" --spawn=session` from their chosen working directory (the `--spawn=session` flag is required so the session name applies deterministically instead of being overridden by the hostname in default pool mode)
- **AND** the page instructs the user to paste the resulting session URL into Decenza Settings → AI → Discuss app → Claude Desktop

#### Scenario: Platform compatibility text is accurate
- **WHEN** the user reads the Platform Compatibility section
- **THEN** the page does NOT claim that Claude iOS or Android apps are incompatible
- **AND** the page correctly describes that Claude iOS/Android apps can connect to a Decenza session via Claude Code Remote Control

### Requirement: User-Managed Session Lifecycle
The user SHALL own the lifecycle of the `claude remote-control` subprocess. Decenza does not spawn, monitor, or restart any external process in this POC.

#### Scenario: Decenza is restarted while a session is running
- **WHEN** the user restarts Decenza while their `claude remote-control` process is still running in their terminal
- **THEN** the stored `claudeRcSessionUrl` persists across the restart
- **AND** the Discuss button continues to open the same session URL

#### Scenario: The user's remote-control process dies
- **WHEN** the user's terminal hosting `claude remote-control` is closed or the process crashes
- **THEN** Decenza has no awareness of the process state
- **AND** tapping the Discuss button opens the stale URL — recovery is the user's responsibility

### Requirement: Fixed Session Identity
The user SHALL launch the Remote Control session with a fixed name ("Decenza_REMOTE") that identifies it in the Claude app session list. The `_REMOTE` suffix makes it unambiguous that this is the remote-control session, not a general Decenza reference. Bean context is communicated through MCP, not the session title.

#### Scenario: User finds the Decenza session in the Claude app
- **WHEN** the user opens the Claude app session list
- **THEN** the Decenza session is visible by name
- **AND** tapping it opens the ongoing dialing conversation

### Requirement: Current Dialing Context MCP Resource
The MCP server SHALL expose a `decenza://dialing/current_context` resource providing a compact snapshot of the current bean, grinder setting, recent shots, active profile, and machine state.

#### Scenario: Claude reads dialing context at turn start
- **WHEN** the user sends a message in the Decenza Remote Control session
- **THEN** Claude can access current bean, grinder, recent shot data, active profile, and machine phase from MCP without the user providing it
- **AND** Claude responses are grounded in current Decenza state from the first message

#### Scenario: Resource is called before shot history is ready
- **WHEN** the resource is called before `ShotHistoryStorage::isReady()` returns true
- **THEN** the resource returns bean, grinder, active profile, and machine phase from in-memory state
- **AND** `recentShots` is an empty array rather than failing the request

### Requirement: Self-Updating Agent Instructions
The MCP server SHALL expose a `get_agent_file` tool returning the current `CLAUDE.md` content and a version string tied to the Decenza app version. The user's working-directory `CLAUDE.md` SHALL update itself from this tool at each session start so agent instructions stay current with the app without user intervention.

#### Scenario: Decenza app is updated with improved agent instructions
- **WHEN** the user starts a new Remote Control session after updating Decenza
- **THEN** Claude calls `get_agent_file`, detects the newer version, and overwrites `CLAUDE.md`
- **AND** the updated instructions take effect for that session

#### Scenario: Agent file is already current
- **WHEN** the returned version matches the version in the current `CLAUDE.md`
- **THEN** Claude skips the overwrite and proceeds with the existing file

#### Scenario: Version substitution in the returned content
- **WHEN** the tool returns `content`
- **THEN** the `{{VERSION}}` placeholder in the bundled template is replaced with the current `VERSION_STRING` before the content leaves the tool

### Requirement: Claude Self-Bootstrap via MCP
Once MCP is configured via `.mcp.json` and a Remote Control session is running, the user SHALL be able to complete AI chat setup by asking Claude to set up Decenza AI chat. Claude uses `get_agent_file` and its own filesystem tools — no scripts or additional setup steps required.

#### Scenario: User sets up AI chat for the first time
- **WHEN** the user asks Claude to set up Decenza AI chat in their first Remote Control session
- **THEN** Claude calls `get_agent_file`, creates the `dialing/` subdirectory in the current working directory, and writes `CLAUDE.md`
- **AND** subsequent sessions use that `CLAUDE.md` automatically

### Requirement: Per-Bean Conversation Log
The working directory where the user runs `claude remote-control` SHALL contain a `CLAUDE.md` that instructs Claude to maintain per-bean log files under `dialing/`. Claude reads and writes these files directly. Decenza has no access to or awareness of them.

#### Scenario: Session starts after a reboot
- **WHEN** the user starts a new Remote Control session after the previous one ended
- **THEN** Claude reads the active bean's log file and restores prior context
- **AND** the conversation continues with awareness of previous conclusions and grind decisions

#### Scenario: Claude appends to the log after a discussion
- **WHEN** the user concludes a dialing discussion
- **THEN** Claude appends a concise summary to `dialing/{beanBrand} {beanType}.md` including conclusions, next steps, and any relevant shot data
- **AND** this summary is available in future sessions

#### Scenario: New bean with no log yet
- **WHEN** the active bean has no existing log file
- **THEN** Claude creates `dialing/{beanBrand} {beanType}.md` and begins the log with current MCP context

