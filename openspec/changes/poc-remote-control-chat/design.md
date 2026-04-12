## Context

Decenza already has a Discuss button on PostShotReviewPage and ShotDetailPage. It currently opens a configured AI app (Claude App, Claude Web, ChatGPT, Gemini, Grok, or custom URL) via `Settings.openDiscussUrl()` (which uses `QDesktopServices::openUrl()` on most platforms, and `SFSafariViewController` on iOS for Claude Desktop mode to avoid universal-link interception). The URL is generic — it opens the app home, not a specific conversation.

Claude Code Remote Control runs a persistent session on the user's desktop, accessible from the Claude iOS/Android app, Claude desktop app, or claude.ai in a browser. The MCP server in Decenza already provides live espresso context.

The goal of this POC is to make the Discuss button open the **right conversation** with as little code as possible, so we can validate whether a dialing-journal experience is compelling before committing to a full bridge service.

## Goals / Non-Goals

- **Goals:**
  - Upgrade the existing Discuss button to open a persistent, bean-aware session
  - Give Claude immediate context from MCP so the user doesn't need to explain what they're working on
  - Keep agent instructions (`CLAUDE.md`) self-updating via MCP so they evolve with the app
  - Minimal code changes — work within the existing `discussShotApp` / `discussShotUrl` model where possible

- **Non-Goals:**
  - Per-bean separate sessions (one persistent session is a running journal — richer for cross-bean comparison)
  - Native Decenza chat UI (that's the bridge spec)
  - Supporting multiple simultaneous sessions
  - **Automated subprocess management** (spawning `claude remote-control` from Decenza via `QProcess`) — descoped, see Decision below
  - **Automated MCP config writing** (writing `~/.claude/...` config files from Decenza) — descoped, see Decision below

## Decisions

### Decision: User Owns the `claude remote-control` Lifecycle

The user runs `claude remote-control --name "Decenza_REMOTE" --spawn=session` themselves in a terminal on their desktop, copies the session URL it prints, and pastes it into Decenza Settings → AI → Discuss app → Claude Desktop. Decenza stores the URL and opens it when the Discuss button is tapped.

**Why this instead of QProcess auto-management:**

1. **Works uniformly across all Decenza platforms.** Most users run Decenza on an Android tablet, where there's no local `claude` binary to spawn — the `claude remote-control` process must run on a separate desktop regardless. Building a QProcess manager that only works when Decenza is on Mac/Win/Linux adds platform-specific code with limited benefit.
2. **POC scope.** Automation is throwaway complexity if the POC fails. The question the POC needs to answer is "is the experience good enough" — not "can we automate setup cleanly." Manual setup is a one-time step for the user.
3. **Same Discuss button code path everywhere.** Decenza only stores + opens a URL. No platform gating, no process monitoring, no stdout parsing.
4. If the POC passes, automation can be added later as a follow-up (see the original task list Steps 6–7, preserved as DESCOPED in `tasks.md`).

### Decision: `.mcp.json` Instead of Global Claude Code Config

Claude Code auto-discovers an `.mcp.json` file in the current working directory when launched. Instead of writing to a platform-specific global config (`~/.claude.json`, etc.), the user creates a working directory (e.g., `~/Decenza-AI/`) and drops in an `.mcp.json` with Decenza's MCP URL pre-filled. This is:

- **Scoped:** the MCP server is registered only for sessions launched from that directory, so Decenza doesn't pollute the user's global Claude Code config
- **Portable:** same instructions work on Mac/Win/Linux because `.mcp.json` is Claude Code's own cross-platform mechanism
- **Paste-able:** the `/mcp/setup` page renders the `.mcp.json` contents with the correct Decenza MCP URL substituted in live, so the user just copies and saves

### Decision: Single Persistent Session as a Dialing Journal

One `claude remote-control --name "Decenza_REMOTE" --spawn=session` session runs persistently. `--spawn=session` keeps the server in single-session mode so `--name` applies deterministically (in the default `same-dir` spawn mode, Claude pools up to 32 on-demand sessions whose titles fall back to the machine hostname). The session is not reset between beans. The conversation accumulates across all beans over time, functioning as a dialing journal. Users can ask cross-bean questions ("how did the Ethiopia compare to last month's Kenya?") because the full history is available.

### Decision: Fixed Session Name "Decenza_REMOTE"

**Verified (April 2026):** `/rename` via stdin does not work in Remote Control server mode. The session name is fixed at launch via `--name`. The session is named "Decenza_REMOTE" permanently — the `_REMOTE` suffix makes it unambiguous in the Claude session list where a user might also see other Decenza-related work. Users find their session by this name in the Claude app session list.

Bean context is communicated through the `current_dialing_context` MCP resource, not the session title.

### Decision: `current_dialing_context` MCP Resource

An async MCP resource at `decenza://dialing/current_context` that Claude reads at the start of each turn:

```json
{
  "bean": { "brand": "...", "type": "...", "roastDate": "...", "doseWeightG": 18.0 },
  "grinder": { "brand": "...", "model": "...", "setting": "..." },
  "recentShots": [
    { "id": 123, "timestamp": "2026-04-11T09:12:00-06:00",
      "profileName": "...", "doseG": 18.0, "yieldG": 36.2, "durationSec": 28.4,
      "tdsPercent": 9.2, "extractionYieldPercent": 21.1 }
  ],
  "activeProfile": { "name": "...", "editorType": "..." },
  "machinePhase": "idle"
}
```

Implemented in `src/mcp/mcpresources.cpp` using the existing `registerAsyncResource()` + `QThread::create()` + `withTempDb()` pattern. Bean/grinder fields are captured on the main thread from `Settings` before the background thread starts (Settings getters are main-thread-only). DB query follows the same column naming as `decenza://shots/recent` — `drink_tds`, `drink_ey`, `dose_weight`, `final_weight`, `duration_seconds`.

**Signature change:** `registerMcpResources()` now also takes `Settings*`, updated at the call site in `mcpserver.cpp`.

### Decision: Per-Bean Log Files on the Host Machine

Conversation history is persisted as markdown files on the host machine (where `claude remote-control` runs). Claude reads and writes these files directly via filesystem. Decenza has no access to or awareness of these files.

```
{working dir}/
├── .mcp.json             # registers Decenza MCP, created by user from /mcp/setup
├── CLAUDE.md             # self-updates from get_agent_file
└── dialing/
    ├── Ethiopia Yirgacheffe.md
    ├── Kenya Kiambu.md
    └── ...
```

**CLAUDE.md instructs Claude to:**
- At session start: call `get_agent_file` to self-update if newer; call `current_dialing_context` to identify the active bean; read `dialing/{beanBrand} {beanType}.md` for prior history
- During conversation: reference the log for prior grind settings, conclusions, and decisions
- After each discussion: append a concise summary with conclusions, next steps, and relevant shot data
- Create the log file if it does not exist yet

**Decenza's role:** provide live context via MCP only. No file reading, no file writing, no knowledge of the log directory.

### Decision: No Setup Scripts — Claude Self-Bootstraps via MCP

Once the user has `.mcp.json` saved and a Remote Control session running, Claude already has everything needed:
- `get_agent_file` MCP tool to fetch `CLAUDE.md` content and version
- Its own filesystem tools to create directories and write files

Setup is triggered by the user saying *"Set up Decenza AI chat"* (or equivalent) in their first session. Claude calls `get_agent_file`, writes `CLAUDE.md` in the working directory, and creates `dialing/`. No scripts, no new web endpoints.

### Decision: `get_agent_file` MCP Tool for Self-Updating CLAUDE.md

A sync MCP tool in `src/mcp/mcptools_agent.cpp` (new file) that returns:

```json
{ "version": "1.6.6", "content": "<CLAUDE.md text>" }
```

Version comes from the `VERSION_STRING` macro (generated from `src/version.h.in`, tied to `project(Decenza VERSION x.y.z)` in `CMakeLists.txt`). Content is loaded from `:/ai/claude_agent.md` (bundled resource), with a `{{VERSION}}` placeholder substituted at tool-call time so the returned content always has the live version in its header.

`CLAUDE.md` contains a `<!-- decenza-agent-version: ... -->` header and instructs Claude to:
1. Call `get_agent_file` at session start
2. Compare the returned version to the one in the file's header
3. If newer, overwrite `CLAUDE.md` with the new content and use the updated instructions for this session

This means agent instructions evolve with Decenza app updates without any user intervention. Users never manually edit `CLAUDE.md`.

### Decision: Claude Desktop as a New `discussShotApp` Option (Appended at Index 7)

The existing `discussShotApp` enum had indexes 0–5 for specific apps and 6 for "None". Adding "Claude Desktop" at index 7 (rather than inserting before "None") avoids a migration: users who had `discussShotApp = 6` (None) keep that setting unchanged. The dropdown UX has "None" in the middle of the list, which is acceptable for a POC.

`discussShotUrl()` now returns `claudeRcSessionUrl` when `discussShotApp == 7`. All Discuss call sites route through `Settings.openDiscussUrl()`, which on iOS uses `SFSafariViewController` for Claude Desktop mode (to bypass universal-link interception on older iPads) and `QDesktopServices::openUrl()` everywhere else. `DiscussItem.qml`, `PostShotReviewPage.qml`, and `ShotDetailPage.qml` all gate their `enabled` state on `isClaudeDesktopReady` so the button is disabled (but visible) until the user pastes a URL.

Two new constants are exposed via `Q_PROPERTY`:
- `discussAppNone` (unchanged, = 6) — already used in QML visibility checks
- `discussAppClaudeDesktop` (new, = 7) — used in SettingsAITab.qml and DiscussItem.qml

## Verified Findings (April 2026)

- **iOS connection works** — Claude iOS app connects to a Remote Control session successfully. On iOS, the session URL opens in an in-app `SFSafariViewController` (to avoid universal-link interception on iPads running iOS 17 where the Claude app lacks the Code tab); on all other platforms, it opens via the system URL handler.
- **Desktop Claude app connects too** — so Remote Control clients include iOS, Android, Claude desktop app (Mac/Win), and claude.ai in a browser. Non-iOS platforms deep-link into whichever client the user's device has installed via `QDesktopServices::openUrl()`.
- **`/rename` via stdin does not work** — session name is fixed at launch; must use `--name "Decenza_REMOTE"` at startup

## Open Questions (Deferred to POC Evaluation)

These were originally in Section 0 of the task list as blocker verifications. With the user-owned process model, none of them block implementation — they're things to observe during POC evaluation (Section 9 of `tasks.md`):

- Does the session URL (`https://claude.ai/code/sessions/{id}`) deep-link into the Claude iOS/Android app directly, or browser only? *(Validated by tapping Discuss during POC eval §9.1)*
- Does session history persist on claude.ai after the local process is killed, and can a new session be started that references the old one? *(Validated by §9.4 persistence test and §9.5 multi-day test — note the per-bean log file provides a fallback source of continuity even if the upstream session is gone.)*
