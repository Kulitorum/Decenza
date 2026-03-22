# MCP Server for Decenza

## Context

Add an MCP (Model Context Protocol) server to Decenza so AI assistants (Claude Desktop, etc.) can fully monitor and control the DE1 espresso machine over the network. The MCP server rides on the existing ShotServer HTTP infrastructure at `/mcp`, reusing TLS, auth, and socket management. The user configures MCP behavior (on/off, access level, confirmation level) in the AI settings tab, which gets reorganized into sections to accommodate the new controls.

## Architecture

### Transport: Streamable HTTP (MCP 2025-03-26 spec)

- **POST /mcp** — JSON-RPC 2.0 requests (initialize, tools/call, resources/read, etc.)
- **GET /mcp** — SSE stream for server-initiated notifications (resource changes)
- **DELETE /mcp** — terminate session
- Session tracked via `Mcp-Session` header (separate from auth cookies)

### File Structure

```
src/mcp/
  mcpserver.h/cpp           — Session management, JSON-RPC dispatch, SSE
  mcpsession.h/cpp          — Per-client state (capabilities, SSE socket, subscriptions)
  mcptoolregistry.h/cpp     — Tool definitions registry + dispatch
  mcpresourceregistry.h/cpp — Resource definitions registry
  mcptools_machine.cpp      — Machine control + state tools
  mcptools_shots.cpp        — Shot history + feedback tools
  mcptools_profiles.cpp     — Profile management tools
  mcptools_settings.cpp     — Settings read/write tools
  mcptools_dialing.cpp      — Dial-in conversation tools (context bundle, suggest/apply changes)
```

### Class Design

**McpServer** — main coordinator, owned by main.cpp stack:
- Receives HTTP requests forwarded from ShotServer for `/mcp` paths
- Manages sessions (`QHash<QString, McpSession*>`)
- Dispatches JSON-RPC methods to tool/resource registries
- Checks `Settings::mcpEnabled()` — returns 404 for all `/mcp` requests when disabled
- Checks `Settings::mcpAccessLevel()` — filters tool list and rejects calls based on level
- Checks `Settings::mcpConfirmationLevel()` — decides which ops need UI confirmation
- Dependency injection: DE1Device, MachineState, MainController, ShotHistoryStorage, Settings

**McpSession** — per-client:
- Session ID (UUID), created/lastActivity timestamps
- `initialized` flag (set after handshake)
- Client capabilities from initialize
- Subscribed resource URIs
- SSE socket (QPointer, nullable)

**McpToolRegistry** — tool definitions + handlers:
- `registerTool(name, description, inputSchema, handler, category)` — category is "read", "control", or "settings"
- `listTools(accessLevel)` → JSON array filtered by access level
- `callTool(name, arguments, accessLevel, confirmationLevel)` → JSON result
- `hasTool(name)` → bool

**McpResourceRegistry** — resource definitions + readers:
- `registerResource(uri, name, description, mimeType, reader)`
- `listResources()` → JSON array
- `readResource(uri)` → JSON content

## Settings: MCP Configuration (new `Settings` properties)

```cpp
// MCP Server settings
Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
Q_PROPERTY(int mcpAccessLevel READ mcpAccessLevel WRITE setMcpAccessLevel NOTIFY mcpAccessLevelChanged)
Q_PROPERTY(int mcpConfirmationLevel READ mcpConfirmationLevel WRITE setMcpConfirmationLevel NOTIFY mcpConfirmationLevelChanged)
```

### Access Levels (mcpAccessLevel)
| Value | Name | Description |
|-------|------|-------------|
| 0 | Monitor Only | Read machine state, telemetry, shot history, profiles — no control |
| 1 | Control | Everything in Monitor + start/stop operations, skip frame, wake/sleep |
| 2 | Full Automation | Everything in Control + upload profiles, change settings, set active profile |

### Confirmation Levels (mcpConfirmationLevel)
| Value | Name | Description |
|-------|------|-------------|
| 0 | None | All allowed commands execute immediately |
| 1 | Dangerous Only | Start operations (espresso/steam/hotwater/flush), profile uploads, settings changes require UI confirmation |
| 2 | All Control | Every machine control and write operation requires confirmation |

### Tool Category → Access Level Mapping

Each tool has a `category` that determines the minimum access level required:

| Category | Min Access Level | Tools |
|----------|-----------------|-------|
| `read` | 0 (Monitor) | machine_get_state, machine_get_telemetry, shots_list, shots_get_detail, shots_get_debug_log, shots_compare, profiles_list, profiles_get_active, profiles_get_detail, profiles_get_params, settings_get, dialing_get_context |
| `control` | 1 (Control) | machine_wake, machine_sleep, machine_start_espresso, machine_start_steam, machine_start_hot_water, machine_start_flush, machine_stop, machine_skip_frame, shots_update |
| `settings` | 2 (Full) | profiles_set_active, profiles_edit_params, profiles_save, profiles_delete, profiles_create, shots_delete, settings_set |

### Tool → Confirmation Level Mapping

Two confirmation mechanisms are used depending on where the user is:

- **In-app dialog**: For machine start operations — the user is physically at the machine and must approve on screen
- **Chat-based**: For settings/data operations — the user is at their desk interacting with the AI remotely

| Tool | Dangerous Only (1) | All Control (2) | Mechanism |
|------|-------------------|-----------------|-----------|
| machine_start_* | **Confirm** | Confirm | In-app dialog |
| machine_wake/sleep | No confirm | Confirm | Chat |
| machine_stop | No confirm | Confirm | Chat |
| machine_skip_frame | No confirm | Confirm | Chat |
| profiles_set_active | **Confirm** | Confirm | Chat |
| profiles_edit_params | **Confirm** | Confirm | Chat |
| profiles_save | **Confirm** | Confirm | Chat |
| profiles_delete | **Confirm** | Confirm | Chat |
| profiles_create | **Confirm** | Confirm | Chat |
| shots_delete | **Confirm** | Confirm | Chat |
| settings_set | **Confirm** | Confirm | Chat |
| shots_update | No confirm | No confirm | — |

When confirmation level is 0 (None), all tools execute immediately regardless of mechanism.

### In-App Confirmation (machine_start_* tools)

For operations that physically affect the machine, confirmation happens on the device screen where the user can see and control the machine:

1. `McpServer::handleToolsCall()` detects the tool needs in-app confirmation
2. The HTTP response is **held** — stored in `PendingConfirmation` with a `QPointer<QTcpSocket>`
3. McpServer emits `confirmationRequested(toolName, toolDescription, sessionId)`
4. QML shows `McpConfirmDialog`: "An AI assistant wants to: Start pulling an espresso shot. Allow?"
5. The dialog has a **15-second auto-dismiss timer** (legitimate UI auto-dismiss per CLAUDE.md) that denies by default
6. The dialog's `onClosed` signal drives the C++ callback — not the raw timer. The timer only closes the dialog UI; `onClosed` then calls `McpServer::confirmationResolved(sessionId, accepted)`
7. `confirmationResolved` executes the tool (if accepted) or returns an error (if denied), sending the held HTTP response

**Edge cases**: If the socket disconnects while waiting, the response is dropped (logged). If a new confirmation arrives while one is pending, the old one is auto-denied first.

**McpConfirmDialog implementation notes**:
- Use `property string toolDescription` for the text — NOT `message` or `description`, which are FINAL on `Dialog` in Qt 6.10+
- Use `AccessibleButton` for Confirm/Deny buttons — not raw `Rectangle+MouseArea`
- Announce dialog text via `AccessibilityManager.announce()` when opened

### Chat-Based Confirmation (settings/data tools)

For operations where the user is at their desk interacting with the AI remotely (not at the machine):

1. `McpServer::handleToolsCall()` checks if the tool needs chat confirmation and `"confirmed"` is not in the arguments
2. If not confirmed, returns immediately with: `{"needs_confirmation": true, "action": "settings_set", "description": "Change machine settings", "parameters": {...}}`
3. The AI sees this response and asks the user in chat: "I'd like to change the brew temperature to 94°C. Should I proceed?"
4. If the user approves, the AI re-calls the tool with `"confirmed": true` in the arguments
5. The tool executes normally (the `confirmed` key is stripped before passing to the handler)

This avoids holding HTTP connections and works naturally with the conversational AI flow. The `confirmed` parameter is declared in each tool's `inputSchema` so the AI knows to include it.

## Tools (Full Set)

### Machine Control
| Tool | Description | Category |
|------|-------------|----------|
| `machine_wake` | Wake from sleep | control |
| `machine_sleep` | Put to sleep | control |
| `machine_start_espresso` | Start pulling a shot. Optional brew overrides (dose, yield, temperature, grind) apply for this shot only, matching QML BrewDialog. | control |
| `machine_start_steam` | Start steaming | control |
| `machine_start_hot_water` | Dispense hot water | control |
| `machine_start_flush` | Flush group head | control |
| `machine_stop` | Stop current operation | control |
| `machine_skip_frame` | Skip to next profile frame | control |

### Machine State
| Tool | Description | Category |
|------|-------------|----------|
| `machine_get_state` | Phase, connection, readiness, heating status, water level (ml + mm), firmware version, active profile name | read |
| `machine_get_telemetry` | Live pressure, flow, temp, weight, goal values. During a shot, also returns the current shot's time-series data so far (not just the latest sample) so the AI can detect channeling or stalling mid-shot. | read |

### Shot History
| Tool | Description | Category |
|------|-------------|----------|
| `shots_list` | List shots with filters (limit, offset, profile, bean, enjoyment, after/before date range) | read |
| `shots_get_detail` | Full shot record with time-series data | read |
| `shots_get_debug_log` | Per-shot debug log (BLE frames, phase transitions, SAW events, flow calibration). Paginated with offset/limit. | read |
| `shots_compare` | Side-by-side comparison of 2+ shots with auto-computed change diffs (grind, dose, yield, duration) | read |
| `shots_update` | Update any metadata field on a shot: enjoyment, notes, dose, yield, bean info, grinder info, barista, TDS, EY. Same fields the QML shot editor can change. Replaces the old `shots_set_feedback`. | control |
| `shots_delete` | Delete a shot by ID. Permanent and cannot be undone. | settings |

### Profile Management
| Tool | Description | Category |
|------|-------------|----------|
| `profiles_list` | List all available profiles | read |
| `profiles_get_active` | Get current profile name + details | read |
| `profiles_get_detail` | Full profile JSON by filename | read |
| `profiles_get_params` | Get the current profile's editable recipe parameters, tailored to its editor type (dflow, aflow, pressure, flow). Returns all parameters that can be passed to `profiles_edit_params`. | read |
| `profiles_set_active` | Load and activate a profile | settings |
| `profiles_edit_params` | Edit the current profile's recipe parameters and regenerate frames. Only provide fields you want to change — unspecified fields keep their current values. Triggers frame regeneration and uploads to machine. Profile is marked modified but not saved to disk — call `profiles_save` to persist. | settings |
| `profiles_save` | Save the current (modified) profile to disk. Without this, edits are active on the machine but lost if another profile is loaded. Optionally provide filename + title for Save As. | settings |
| `profiles_delete` | Delete a user/downloaded profile. For built-in profiles, removes local overrides and reverts to the original built-in version. | settings |
| `profiles_create` | Create a new blank profile with a given editor type (dflow, aflow, pressure, flow, advanced) and title. Uses the same creation functions as the QML UI. | settings |

### Settings
| Tool | Description | Category |
|------|-------------|----------|
| `settings_get` | Read all app settings, specific keys, or a category. Categories: preferences, connections, screensaver, accessibility, ai, espresso, steam, water, flush, dye, mqtt, themes, visualizer, update, data, history, language, debug, battery, heater, autofavorites. Sensitive fields (API keys, passwords) are excluded. | read |
| `settings_set` | Update any app setting across all QML Settings tabs. Covers 100+ fields: preferences, connections, screensaver, accessibility, AI, espresso, steam, water, flush, DYE, MQTT, themes, visualizer, update, data, history, language, debug, battery, heater, auto-favorites. Sensitive fields (API keys, passwords) excluded. | settings |

### AI Dial-In Conversation (key feature)

The MCP enables an external AI (e.g. Claude Desktop) to act as a dial-in advisor with full machine context. Unlike the in-app AI which uses a cloud provider and limited context, an MCP-connected AI can use its own capabilities with direct access to machine tools.

| Tool | Description | Category |
|------|-------------|----------|
| `dialing_get_context` | Get full dial-in context bundle: current profile recipe + profile knowledge + recent shot summary (via `ShotSummarizer`) + dial-in history (last N shots with same profile family via `ShotHistoryStorage::getRecentShotsByKbId()`) + bean metadata + dial-in reference tables (`docs/ESPRESSO_DIAL_IN_REFERENCE.md`). This is the primary read tool for dial-in — a single call gives the AI everything it needs to analyze a shot and suggest changes. | read |
| ~~`dialing_suggest_change`~~ | **Removed.** Was a no-op stub that returned `"suggestion_displayed"` without actually displaying anything or changing settings. The AI mistakenly treated it as applying changes (e.g., grind size). Use `settings_set` to change grind (`dyeGrinderSetting`), dose (`dyeBeanWeight`), yield (`targetWeight`), temperature (`espressoTemperature`), etc. | — |
| ~~`dialing_apply_change`~~ | **Removed.** Was a convenience wrapper that duplicated `settings_set` + `profiles_set_active`. Caused the advanced-profile-corruption bug due to duplicated code paths. Use `settings_set` for temp/weight/DYE changes and `profiles_set_active` for profile switches. | — |

**Design note — why one read tool instead of four:** An earlier version had separate `dialing_get_shot_summary`, `dialing_get_history`, `dialing_get_profile_knowledge`, and `dialing_get_context` tools. These were consolidated into just `dialing_get_context` because: (1) the MCP client almost always needs all the context together, (2) fewer tools means better MCP client compatibility and less confusion for the AI, (3) the individual data is still available via generic tools (`shots_get_detail`, `profiles_get_detail`, `settings_get`) if needed. The `dialing_get_context` tool accepts optional parameters to control what's included (e.g., `shot_id` to analyze a specific shot instead of the most recent, `history_limit` to control how many prior shots to include).

**How the dial-in flow works via MCP:**

1. User pulls a shot → AI gets notified via SSE (`decenza://shots/recent` resource update)
2. AI calls `dialing_get_context` to get the full picture (shot data, profile knowledge, history, reference tables)
3. AI analyzes the shot using its own reasoning (no cloud API call needed — the AI IS the reasoning engine)
4. AI responds in chat with its recommendation
5. If user approves, AI calls `settings_set` to adjust grind/dose/yield/temperature
6. AI can ask the user how the shot tasted and call `shots_update` to record enjoyment/notes
7. Next shot: repeat, with the AI seeing the progression via `dialing_get_context` history

**What context is available to the MCP AI:**

| Context Layer | Source | Tool |
|---------------|--------|------|
| Profile recipe (frame-by-frame) | Profile JSON | `dialing_get_context` / `profiles_get_detail` |
| Profile knowledge (roast suitability, expected curves) | `resources/ai/profile_knowledge.md` via `ShotSummarizer::s_profileKnowledge` | `dialing_get_context` |
| Shot data (curves, phases, anomalies) | `ShotSummarizer` | `dialing_get_context` / `shots_get_detail` |
| Dial-in history (last N shots, same profile) | `ShotHistoryStorage::getRecentShotsByKbId()` | `dialing_get_context` |
| Bean metadata (brand, type, roast, grinder, burrs) | Shot metadata / Settings DYE | `dialing_get_context` |
| Machine telemetry (live pressure/flow/temp) | `MachineState` / `DE1Device` | `machine_get_telemetry` |
| All available profiles | Profile list | `profiles_list` |
| Dial-in reference tables | `docs/ESPRESSO_DIAL_IN_REFERENCE.md` | `dialing_get_context` (included in bundle) |
| Water level | `DE1Device::waterLevelMl()` / `waterLevelMm()` | `machine_get_state` |

The MCP AI has a significant advantage over the in-app AI: it's not limited by token budgets or cloud API costs. It can read the full profile knowledge base, all dial-in reference tables, and maintain a long conversation across multiple shots without worrying about context trimming.

## AI-Friendly Data Conventions

MCP tool responses are consumed by LLMs (Claude, ChatGPT, etc.) which cannot reliably interpret raw numbers without context. All numeric fields in MCP responses follow these conventions:

- **Timestamps**: ISO 8601 with timezone offset (e.g., `"2026-03-21T11:20:41-06:00"`), never Unix epoch
- **Units in field names**: Numeric fields include their unit as a suffix — `doseG` (grams), `pressureBar` (bar), `temperatureC` (Celsius), `flowMlPerSec` (ml/s), `durationSec` (seconds), `weightG` (grams), `targetVolumeMl` (ml)
- **Scales in field names**: Bounded values indicate their range — `enjoyment0to100` (0-100 rating)
- **Human-readable enums**: Machine phases, editor types, and states are returned as strings (`"idle"`, `"pouring"`, `"dflow"`), not numeric codes

When adding new MCP tool responses, never return raw numbers that require domain knowledge to interpret. An AI seeing `"pressure": 9.0` doesn't know if that's bar, PSI, or kPa. Use `"pressureBar": 9.0` instead.

## Resources (SSE Notifications)

| URI | Description | Notification Trigger |
|-----|-------------|---------------------|
| `decenza://machine/state` | Current phase + connection + water level | `MachineState::phaseChanged` |
| `decenza://machine/telemetry` | Live pressure/flow/temp/weight | Throttled 1Hz from shot samples |
| `decenza://profiles/active` | Active profile | `MainController::currentProfileChanged` |
| `decenza://shots/recent` | Last 10 shots | `ShotHistoryStorage::shotSaved` |
| `decenza://profiles/list` | All available profiles | `MainController::profilesChanged` |
| `decenza://debug/log` | Full persisted debug log with memory snapshot | On-demand (no SSE) |
| `decenza://debug/memory` | RSS, peak RSS, QObject count, memory samples | On-demand (no SSE) |

## AI Settings Tab UI Redesign

### Current State
`SettingsAITab.qml` (602 lines) is a flat list: provider buttons → API key → provider-specific config → cost info → test connection → conversation overlay. No sections or grouping.

### New Layout: Two Sections

Reorganize into clearly labeled collapsible/visual sections:

```
┌─────────────────────────────────────────────┐
│ AI Provider                                 │
│ ─────────────────────────────────────────── │
│ [OpenAI] [Anthropic] [Gemini] [OpenRouter] [Ollama] │
│                                             │
│ Claude recommendation banner                │
│                                             │
│ API Key: [••••••••••••••]                   │
│ Get key: console.anthropic.com → API Keys   │
│                                             │
│ Cost: ~$0.01/shot                           │
│ [Test Connection]  ✓ Connected  [Continue Chat] │
│                                             │
├─────────────────────────────────────────────┤
│ MCP Server (AI Remote Control)              │
│ ─────────────────────────────────────────── │
│                                             │
│ Enable MCP Server          [toggle switch]  │
│ Allows AI assistants like Claude Desktop    │
│ to monitor and control your DE1 remotely.   │
│                                             │
│ Access Level:                               │
│ ( ) Monitor Only — read state & history     │
│ (•) Control — + start/stop operations       │
│ ( ) Full Automation — + profiles & settings │
│                                             │
│ Confirmation:                               │
│ ( ) None — commands execute immediately     │
│ (•) Dangerous Only — confirm start ops      │
│ ( ) All Control — confirm every command     │
│                                             │
│ Status: Listening on port 8888              │
│ Active sessions: 1                          │
└─────────────────────────────────────────────┘
```

### Implementation Details

**Section headers**: Use a reusable pattern — bold title text + 1px divider below. Same visual weight as the existing dividers but with labels. Use individual font sub-properties (`font.family: Theme.subtitleFont.family; font.pixelSize: Theme.subtitleFont.pixelSize; font.bold: true`) — do NOT combine `font: Theme.subtitleFont` with `font.bold: true` (QML property conflict).

**MCP section** (visible always, controls enabled only when `mcpEnabled` is true):

1. **Enable toggle**: `StyledSwitch` bound to `Settings.mcpEnabled` with `accessibleName` set. When off, greys out all controls below and MCP server returns 404.

2. **Description text**: Brief explanation of what MCP does, styled like the Claude recommendation banner but neutral.

3. **Access Level**: Three styled radio-like selectors in a `ColumnLayout`, each with name + description. Use the `Rectangle + AccessibleMouseArea` pattern (matching `StringBrowserPage.qml` radio pattern) styled with `Theme` values — do NOT use raw Qt `RadioButton` which renders with unstyled Material appearance. Group in a `ButtonGroup` for mutual exclusion. Bound to `Settings.mcpAccessLevel`. Disabled when MCP is off.

4. **Confirmation Level**: Same styled radio pattern. Bound to `Settings.mcpConfirmationLevel`. Disabled when MCP is off or access level is 0 (monitor-only has nothing to confirm).

5. **Status line**: Shows "Listening on port {ShotServer.port}" when enabled, "Disabled" when off. Shows active session count from `McpServer.activeSessionCount` property.

### QML Components Used
- `StyledSwitch` for the enable toggle (NOT raw `Switch`) — uses project accessibility via `accessibleName` property
- Styled `Rectangle + AccessibleMouseArea` for radio-like access/confirmation selectors (NOT raw `RadioButton`) — grouped in `ButtonGroup`
- `AccessibleButton` for all action buttons
- All text via `TranslationManager.translate()` / `Tr` component
- All styling from `Theme` singleton — no hardcoded colors, fonts, spacing, or radii
- Accessibility: every interactive element must have `Accessible.role`, `Accessible.name`, `Accessible.focusable`, and `Accessible.onPressAction` (or use `AccessibleButton`/`AccessibleMouseArea` which handle this)

### New Settings Properties in `settings.h`

```cpp
// MCP Server
Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
Q_PROPERTY(int mcpAccessLevel READ mcpAccessLevel WRITE setMcpAccessLevel NOTIFY mcpAccessLevelChanged)
Q_PROPERTY(int mcpConfirmationLevel READ mcpConfirmationLevel WRITE setMcpConfirmationLevel NOTIFY mcpConfirmationLevelChanged)
```

Defaults: `mcpEnabled = false`, `mcpAccessLevel = 1` (Control), `mcpConfirmationLevel = 1` (Dangerous Only).

### McpServer QML-Visible Properties

Expose on McpServer for the status display:
```cpp
Q_PROPERTY(int activeSessionCount READ activeSessionCount NOTIFY activeSessionCountChanged)
```

**Implementation note**: The backing `m_sessions` is a `QHash` whose `size()` returns `qsizetype` (64-bit on iOS/macOS). The getter must cast safely: `return static_cast<int>(m_sessions.size())`. The `Q_PROPERTY` type stays `int` for QML binding compatibility.

Register in main.cpp context: `engine.rootContext()->setContextProperty("McpServer", &mcpServer);`

## Integration Points

### ShotServer (`src/network/shotserver.cpp`)

Add route block in `handleRequest()`:
```cpp
if (path == "/mcp" || path.startsWith("/mcp/")) {
    if (!m_mcpServer || !Settings::instance()->mcpEnabled()) {
        sendErrorResponse(socket, 404, "Not Found");
        return;
    }
    m_mcpServer->handleHttpRequest(socket, method, path, headers, body);
    return;
}
```
Add `McpServer* m_mcpServer` member + setter.

**SSE socket ownership**: For GET `/mcp` (SSE stream), ShotServer forwards the request to McpServer. McpServer takes ownership of the socket for the SSE lifetime — it removes the keep-alive timer via `m_keepAliveTimers.take(socket)` (same pattern as existing `m_sseLayoutClients` and `m_sseThemeClients` in ShotServer) and manages the socket directly. When the SSE connection closes, McpServer cleans up the socket.

**Auth for SSE streams**: Auth is validated once at connection time on the initial GET `/mcp` request (same as existing SSE endpoints for layout/theme). The `Mcp-Session` header provides session identity after the initial auth check. No periodic re-validation — if the auth cookie expires, the SSE stream stays open until it's naturally closed or the session times out.

### main.cpp (after ShotServer setup, ~line 700)
```cpp
McpServer mcpServer;
mcpServer.setDE1Device(&de1Device);
mcpServer.setMachineState(&machineState);
mcpServer.setMainController(&mainController);
mcpServer.setShotHistoryStorage(mainController.shotHistory());
mcpServer.setSettings(&settings);
mainController.shotServer()->setMcpServer(&mcpServer);
engine.rootContext()->setContextProperty("McpServer", &mcpServer);
```

### CMakeLists.txt
Add all `src/mcp/*.cpp` to SOURCES and `src/mcp/*.h` to HEADERS. Add `McpConfirmDialog.qml` to QML_FILES. No new Qt modules needed.

### ShotHistoryStorage prerequisite

The `dialing_get_context` tool requires `ShotHistoryStorage::getRecentShotsByKbId(const QString& kbId, int limit)` which does not yet exist (only referenced as a TODO comment at `shothistorystorage.cpp:649`). This must be implemented before the dial-in tools. It should:
- Follow the existing async pattern: `requestRecentShotsByKbId()` on main thread → `QThread::create()` background query → emit `recentShotsByKbIdReady()` signal
- Query: `SELECT ... FROM shots WHERE profile_kb_id = :kbId ORDER BY created_at DESC LIMIT :limit`
- Return summary data (not full time-series) for each shot: id, timestamp (ISO 8601 with timezone), profileName, doseG, yieldG, durationSec, enjoyment0to100, grinderSetting, temperature, notes

## Thread Safety

- **State reads** (telemetry, machine state): Direct reads of cached members — safe from any thread
- **DB queries** (shots): Use `QThread::create()` + `QPointer<QTcpSocket>` pattern (same as ShotServer)
- **Machine commands**: `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to main thread
- **Profile loads**: Must run on main thread via `invokeMethod`
- **Confirmation dialog**: `McpServer` emits `confirmationRequested` signal (arrives on main thread via queued connection). QML shows dialog. On confirm/deny/timeout, QML calls back into `McpServer::confirmationResolved(sessionId, accepted)` which invokes the stored response callback.
- **Settings reads** (access/confirmation level): Thread-safe via QSettings, cached in members with notify signals
- **Settings writes** (`settings_set`): Must dispatch to main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — `QSettings::setValue()` from a background thread is not safe when signals are connected to QML bindings

## Security

1. **Auth**: MCP routes go through existing ShotServer TOTP auth middleware
2. **Master switch**: `mcpEnabled` defaults to `false` — user must opt in
3. **Access levels**: Enforced server-side — tools not in scope return JSON-RPC error `-32603` with message "Access level insufficient"
4. **State validation**: Machine control tools verify machine is in valid state before executing (e.g., can't start espresso if not in Ready phase)
5. **Rate limiting**: Max 10 `control` + `settings` category calls/minute per session. Only successful calls count against the limit. Exceeded → JSON-RPC error `-32000` with message "Rate limit exceeded"
6. **Session expiry**: 30-minute inactivity timeout, cleaned by periodic timer
7. **SSE limits**: Max 4 concurrent MCP SSE connections
8. **Session limits**: Max 8 total MCP sessions. New connections beyond the limit receive JSON-RPC error `-32000` with message "Too many sessions"
9. **Uninitialized sessions**: Tool/resource calls before `initialize` handshake return JSON-RPC error `-32600` (Invalid Request) with message "Session not initialized"

## Discuss Shot Feature

### Overview

A "Discuss" button lets the user jump from the shot review screen (or a layout widget) to an external AI app to discuss their shot. When MCP is connected, the external AI can pull full shot context itself via `dialing_get_context`. When MCP is not connected, the app copies a formatted shot summary to the clipboard so the user can paste it.

### Settings

```cpp
// Discuss Shot
Q_PROPERTY(int discussShotApp READ discussShotApp WRITE setDiscussShotApp NOTIFY discussShotAppChanged)
Q_PROPERTY(QString discussShotCustomUrl READ discussShotCustomUrl WRITE setDiscussShotCustomUrl NOTIFY discussShotCustomUrlChanged)
```

**`discussShotApp`** (int enum):
| Value | Name | URL / Deep Link | Notes |
|-------|------|-----------------|-------|
| 0 | Claude App | `claude://` | Opens Claude Desktop app via URL scheme — best with MCP, can pull shot data directly via tools |
| 1 | Claude Web | `https://claude.ai/new` | Opens claude.ai in browser — for users without the desktop app |
| 2 | ChatGPT | `https://chatgpt.com/` | OpenAI's assistant |
| 3 | Gemini | `https://gemini.google.com/app` | Google's AI |
| 4 | Grok | `https://grok.com/` | xAI's assistant |
| 5 | Custom URL | Uses `discussShotCustomUrl` value | For self-hosted models (Ollama web UI, etc.) or any other AI service |

Default: `0` (Claude).

**`discussShotCustomUrl`** (QString): User-entered URL for the "Custom" option. Example: `https://localhost:8080` (Ollama web UI), `https://my-ai.example.com/chat`. Default: empty.

### UI Placement: Settings

Add a **"Discuss Shot"** subsection at the bottom of the MCP section in SettingsAITab.qml (visible regardless of MCP enabled state, since this feature works both with and without MCP):

```
│ Discuss Shot                                │
│                                             │
│ Open in: [Claude            ]  ← tap to open│
│                                             │
│ Custom URL: [________________________]      │  ← only visible when "Custom URL" selected
```

Use a tap-to-open `Dialog { modal: true }` with `AccessibleButton` delegates for each option (Claude, ChatGPT, Gemini, Grok, Custom URL). Do NOT use `StyledComboBox` or `Popup` — TalkBack cannot trap focus inside Qt Popup elements. The trigger button shows the currently selected app name. The Custom URL `StyledTextField` appears below only when "Custom URL" (index 4) is selected. The new `StyledTextField` must be appended to the existing `KeyboardAwareContainer.textFields` array in `SettingsAITab.qml` (the tab is already wrapped in `KeyboardAwareContainer`).

### UI Placement: PostShotReviewPage

Add a "Discuss" button in the bottom bar, next to the existing AI Advice button (sparkle icon). The button:
- Uses a chat/discussion icon (e.g., `qrc:/icons/discuss.svg` — speech bubble with sparkle, or similar)
- Label: "Discuss" (i18n key: `postShotReview.button.discuss`, fallback: `"Discuss"`)
- Same styling as the adjacent AI Advice button (same size, background color, icon+text pattern)

### UI Placement: ShotDetailPage

Same button in the bottom action bar, next to the existing AI Advice button. Uses the same handler logic.

### Behavior on Tap

```
1. Build shot summary text (see format below)
2. Copy to clipboard via QGuiApplication::clipboard()->setText(summary)
   — skip clipboard if MCP is enabled (the AI can pull context via tools)
3. Open the configured app URL via Qt.openUrlExternally(url)
4. Show brief toast: "Shot summary copied — paste in your AI app"
   — or if MCP enabled: "Opening AI app — it has access to your shot data"
```

### Shot Summary Clipboard Format

When MCP is not connected, the clipboard text gives the external AI enough context to be useful:

```
Espresso Shot — [Profile Name]
[DateTime]

Dose: 18.0g → Out: 36.2g (ratio 1:2.01)
Duration: 28.4s
Rating: 82/100

Bean: [Brand] [Type] (roasted [RoastDate])
Roast: [RoastLevel]
Grinder: [Brand] [Model] @ [Setting]
Burrs: [Burrs]

Notes: [espresso_notes if any]

Key metrics:
- Peak pressure: [X] bar during [phase]
- Avg flow: [X] ml/s during pouring
- Temperature: [X]°C

Please help me analyze this shot and suggest improvements for my next extraction.
```

This is built from the same shot data available on PostShotReviewPage (`editShotData`, bean/grinder fields). The `ShotSummarizer` can generate a richer version if the full shot record is available.

### Layout Widget: DiscussItem

A layout widget that opens the configured AI app to discuss the most recent shot. Works from the idle screen without navigating to shot review first.

**File**: `qml/components/layout/items/DiscussItem.qml`

**Behavior**:
- Compact mode (top/bottom bars): Icon + "Discuss" label, tap to open AI app via `AccessibleTapHandler`
- Full mode (center zones): `ActionButton` with discuss icon
- On tap: same behavior as the PostShotReviewPage button, but uses the most recent shot from `ShotHistoryStorage`
- If no shots exist yet, shows disabled state
- No secondary actions (no long-press or double-tap), so `Accessible.description` is not needed

**Registration** (5 places):
1. `CMakeLists.txt` — add `qml/components/layout/items/DiscussItem.qml` to QML_FILES
2. `LayoutItemDelegate.qml` — add case: `case "discuss": src = "items/DiscussItem.qml"; break`
3. `LayoutEditorZone.qml` — add to widget palette (white/actions group) + `getItemDisplayName()` chip label
4. `shotserver_layout.cpp` — add `{type:"discuss",label:"Discuss"}` to `WIDGET_TYPES` + `DISPLAY_NAMES`
5. `LayoutCenterZone.qml` — ensure `"discuss"` is NOT in `isAutoSized()` so it receives fixed action-button sizing (same as `history`, `espresso`, etc.)

### Key Files to Modify (Discuss Feature)

- `src/core/settings.h/cpp` — add `discussShotApp`, `discussShotCustomUrl` properties
- `qml/pages/PostShotReviewPage.qml` — add Discuss button next to AI Advice button in bottom bar
- `qml/pages/ShotDetailPage.qml` — add Discuss button next to AI Advice button
- `qml/pages/settings/SettingsAITab.qml` — add Discuss Shot subsection + append Custom URL field to `KeyboardAwareContainer.textFields`
- `qml/components/layout/LayoutItemDelegate.qml` — add "discuss" case
- `qml/components/layout/LayoutEditorZone.qml` — add to palette + chip label
- `qml/components/layout/LayoutCenterZone.qml` — ensure "discuss" gets fixed action-button sizing
- `src/network/shotserver_layout.cpp` — add to web editor widget list

### Key Files to Create (Discuss Feature)

- `qml/components/layout/items/DiscussItem.qml` — layout widget
- `resources/icons/discuss.svg` — icon (speech bubble or similar)

## Implementation Phases

### Completed

1. ~~**Settings + UI**: Add `mcpEnabled`/`mcpAccessLevel`/`mcpConfirmationLevel`/`discussShotApp`/`discussShotCustomUrl` to Settings. Reorganize SettingsAITab.qml into sections with MCP controls and Discuss Shot subsection.~~ ✅
2. ~~**Discuss Shot feature**: Add Discuss button to PostShotReviewPage and ShotDetailPage. Create DiscussItem layout widget with registration in all 5 places. Implement clipboard summary + `Qt.openUrlExternally()` flow.~~ ✅
3. ~~**Prerequisites**: Implement `ShotHistoryStorage::getRecentShotsByKbId()` for dial-in history queries.~~ ✅
4. ~~**Core protocol**: McpServer, McpSession, JSON-RPC dispatch, ShotServer route integration, CMake setup.~~ ✅
5. ~~**Read-only tools**: machine_get_state, machine_get_telemetry, shots_list, shots_get_detail, shots_compare, profiles_list, profiles_get_active, profiles_get_detail, settings_get.~~ ✅
6. ~~**Dial-in read tool**: dialing_get_context — the highest-value tool for AI dial-in conversations. Bundles shot summary, history, profile knowledge, bean metadata, and reference tables.~~ ✅
7. ~~**Machine control tools**: start/stop operations with access-level gating. Note: start commands only work on DE1 v1.0 headless machines — most machines with GHC require physical button press.~~ ✅
8. ~~**Resources + SSE**: Resource registry, subscriptions, notification wiring (especially `decenza://shots/recent` for dial-in flow).~~ ✅
9. ~~**Write tools**: shots_update, profiles_set_active, settings_set — all with access-level gating.~~ ✅
10. ~~**Polish**: Rate limiting, session cleanup, session limits, API key auth, setup page with install scripts, Claude Desktop integration, help dialog, bridge script.~~ ✅

11. ~~**Scale control tools**: `scale_tare`, `scale_timer_start`, `scale_timer_stop`, `scale_timer_reset`, `scale_get_weight`. Category: control.~~ ✅
12. ~~**Device management tools**: `devices_list`, `devices_scan`, `devices_connect_scale`, `devices_connection_status`. Category: control.~~ ✅
13. ~~**Confirmation dialog**: Hybrid confirmation system — in-app dialog for machine start operations (user is at the machine), chat-based confirmation for settings/data operations (user is at their desk). Maps to `mcpConfirmationLevel` setting (None/Dangerous Only/All Control).~~ ✅

### QML Parity — Remaining Gaps

The following QML capabilities do not yet have MCP equivalents. Organized by priority for achieving full parity as a parallel UI.

#### High Priority (needed for initial release)

14. **Profile creation**: `profiles_create` — create a new blank profile with a given editor type and title. Calls `createNewRecipe()`, `createNewPressureProfile()`, `createNewFlowProfile()`, or `createNewProfile()` depending on editor type. Category: settings.

15. **Shot management**: Replace `shots_set_feedback` with a broader `shots_update` that accepts any metadata field the QML shot editors can change (enjoyment, notes, dose, bean brand/type, roast level/date, grinder brand/model/burrs/setting, barista, TDS, EY). Add `shots_delete` for deleting individual shots. Category: settings.

16. **Brew overrides**: `machine_start_espresso` should accept optional dose/yield/temperature/grind overrides — matching the BrewDialog that QML shows before starting a shot. Calls `activateBrewWithOverrides()`. This lets MCP start a shot with specific parameters without permanently changing the profile.

#### High Priority (needed for QML parity)

16. **Full settings read/write parity**: `settings_get` and `settings_set` must cover every setting the QML Settings tabs expose. Currently only covers espresso/steam/water/DYE fields. Missing settings by tab:

**Preferences**: themeMode, darkThemeName, lightThemeName, autoSleepMinutes, postShotReviewTimeout, extractionView, smartChargingMode, keepSteamHeaterOn, steamDisabled, steamAutoFlush, refillKitOverride, perScreenScale, flowCalibration

**Connections**: savedDE1Address, savedScaleAddress, scaleType, knownScales, usbScaleEnabled

**Screensaver**: screensaverType, screensaverTimeout, screensaverBrightness, screensaverVideos

**Accessibility**: accessibilityEnabled, ttsEnabled, ttsVolume, tickSoundsEnabled, announceExtractionProgress, accessibilityExtractionInterval

**AI**: aiProvider, aiApiKey, aiModel, mcpEnabled, mcpAccessLevel, mcpConfirmationLevel, discussShotApp, discussShotCustomUrl

**History**: shotHistoryGroupBy, shotHistorySortOrder

**Data**: backup/restore operations, database stats

**Options/Steam/Water**: steamTemperature, steamTimeout, steamFlow, steamPitcherPresets, waterTemperature, waterVolume, waterVolumeMode, waterVesselPresets

**Home Automation (MQTT)**: mqttEnabled, mqttBrokerUrl, mqttTopic

**Language**: currentLanguage, availableLanguages

Each setting should use the same `Settings` property that the QML binding uses — no separate code paths.

#### Medium Priority (useful but not blocking)

17. **Visualizer integration**: `visualizer_upload` to upload a shot to visualizer.coffee, `visualizer_import` to import a profile by share code or shot ID.

18. **Profile favorites**: Read/write favorite profiles list. IdlePage shows favorites as quick-switch buttons.

19. **Advanced frame manipulation**: Individual frame operations (`addFrame`, `deleteFrame`, `moveFrame`, `setFrameProperty`). Alternative: rely on `profiles_edit_params` with full steps array (already works).

#### Low Priority (future phases)

20. **Bean inventory system**: Full CRUD for beans and batches. Requires new DB tables and significant UI work.
21. **Real-time streaming**: Subscribe/read for live sensor data during shots. Requires WebSocket or enhanced SSE.
22. **Direct control mode**: Live setpoint adjustment during shots (`setLivePressure`, `setLiveFlow`, `setLiveTemperature`).
23. **Community library**: Browse and download community-shared profiles. Complex async network flow.
24. **Backup/restore**: Create and restore database backups via MCP.

### Restructuring Proposal

Before adding new tools, consolidate existing ones to reduce tool count and avoid duplication:

#### Consolidate `dialing_apply_change` into `settings_set` + `profiles_set_active`

`dialing_apply_change` is a convenience wrapper that duplicates code from `settings_set` (temperature, weight, grinder, bean metadata) and `profiles_set_active` (profile switching). The duplication caused the advanced-profile-corruption bug (fix had to be applied in both places). Removing it simplifies the API — an AI can achieve the same result by calling the individual tools. `dialing_get_context` remains (it has unique functionality). `dialing_suggest_change` was also removed — it was a no-op stub.

**Tools removed**: `dialing_apply_change`
**Migration**: Callers use `settings_set` for temp/weight/DYE changes + `profiles_set_active` for profile switches.

#### Replace `shots_set_feedback` with `shots_update`

`shots_set_feedback` only handles enjoyment + notes, but QML can update any shot metadata field (dose, bean info, grinder info, barista, TDS, EY). Replace with a general `shots_update` that accepts any metadata field. Same underlying function (`requestUpdateShotMetadata`).

**Tools removed**: `shots_set_feedback`
**Tools added**: `shots_update` (superset), `shots_delete`

#### Net tool count change

| Change | Count |
|--------|-------|
| Current tools | 37 |
| Remove `dialing_apply_change` | -1 |
| Remove `shots_set_feedback` | -1 |
| Add `profiles_create` | +1 |
| Add `shots_update` | +1 |
| Add `shots_delete` | +1 |
| **New total** | **38** |

## Phase Status

| # | Phase | Status | Notes |
|---|-------|--------|-------|
| 1 | Settings + UI | ✅ Done | MCP settings, AI tab redesign |
| 2 | Discuss Shot | ✅ Done | Discuss button + layout widget |
| 3 | Prerequisites | ✅ Done | getRecentShotsByKbId() |
| 4 | Core protocol | ✅ Done | McpServer, JSON-RPC, ShotServer routing |
| 5 | Read-only tools | ✅ Done | 9 tools: state, telemetry, shots, profiles, settings |
| 6 | Dial-in read tool | ✅ Done | dialing_get_context context bundle |
| 7 | Machine control | ✅ Done | start/stop with access-level gating |
| 8 | Resources + SSE | ✅ Done | 7 resources, SSE notifications |
| 9 | Write tools | ✅ Done | feedback, profiles, settings, dial-in |
| 10 | Polish | ✅ Done | Rate limiting, sessions, auth, setup, bridge |
| 11 | Scale tools | ✅ Done | tare, timer start/stop/reset, get_weight |
| 12 | Device tools | ✅ Done | list, scan, connect_scale, connection_status |
| 13 | Confirmation dialog | ✅ Done | Hybrid: in-app for start ops, chat for settings |
| 14 | Profile editor tools | ✅ Done | get_params, edit_params, save, delete (all 5 editor types) |
| 15 | QML parity: high | ✅ Done | profiles_create, shots_update/delete, brew overrides, removed dialing_apply_change |
| 16 | Full settings parity | ✅ Done | All QML Settings tabs readable + writable via settings_get/set |
| 17 | QML parity: medium | 🔲 Future | visualizer, favorites, frame manipulation |
| 18 | QML parity: low | 🔲 Future | bean inventory, streaming, direct control, community, backup |

## Verification

1. **UI test**: Toggle MCP on/off, change access/confirmation levels, verify controls enable/disable correctly
2. **Discuss Shot test**: Tap Discuss on PostShotReviewPage → verify clipboard contains shot summary (when MCP off) → verify correct app opens. Test all 5 app options (Claude, ChatGPT, Gemini, Grok, Custom URL). Test with MCP enabled → verify no clipboard copy, toast says "it has access to your shot data".
3. **Discuss layout widget test**: Add DiscussItem to a layout zone. Tap from idle screen → verify it discusses the most recent shot. Verify disabled state when no shots exist.
4. **Integration test with mcp-cli**: `npx @anthropic-ai/mcp-cli` to connect and exercise tools
5. **Access level test**: Set monitor-only, verify control tools are rejected; set control, verify settings tools rejected
6. **Confirmation test**: Set dangerous-only, trigger start espresso from AI, verify dialog appears with 15s timeout, verify async callback delivers response correctly
7. **Dial-in flow test**: Pull shot → verify SSE notification → call dialing_get_context → verify bundle contains shot summary + history + knowledge → call shots_set_feedback → verify enjoyment/notes saved
8. **Rate limit test**: Fire 11 control calls in quick succession, verify 11th is rejected
9. **Session limit test**: Open 9 sessions, verify 9th is rejected with "Too many sessions"
10. **Claude Desktop test**: Add Decenza as MCP server in config, verify tool discovery and execution
11. **Build via Qt Creator** (don't CLI-build)

## Key Files to Modify

- `src/core/settings.h/cpp` — add mcpEnabled, mcpAccessLevel, mcpConfirmationLevel, discussShotApp, discussShotCustomUrl properties
- `src/history/shothistorystorage.h/cpp` — add `getRecentShotsByKbId()` async method
- `qml/pages/settings/SettingsAITab.qml` — reorganize into sections, add MCP controls + Discuss Shot subsection
- `qml/pages/PostShotReviewPage.qml` — add Discuss button next to AI Advice button in bottom bar
- `qml/pages/ShotDetailPage.qml` — add Discuss button next to AI Advice button
- `qml/components/layout/LayoutItemDelegate.qml` — add "discuss" case to switch
- `qml/components/layout/LayoutEditorZone.qml` — add to widget palette + chip label map
- `qml/components/layout/LayoutCenterZone.qml` — ensure "discuss" gets fixed action-button sizing
- `src/network/shotserver_layout.cpp` — add to web editor WIDGET_TYPES + DISPLAY_NAMES
- `src/network/shotserver.h` — add McpServer pointer + setter
- `src/network/shotserver.cpp` — add `/mcp` route dispatch with enabled check
- `src/main.cpp` — wire McpServer with dependencies + QML context property
- `CMakeLists.txt` — add src/mcp/*.cpp, *.h, McpConfirmDialog.qml, DiscussItem.qml

## Key Files to Create

- `src/mcp/mcpserver.h/cpp`
- `src/mcp/mcpsession.h/cpp`
- `src/mcp/mcptoolregistry.h/cpp`
- `src/mcp/mcpresourceregistry.h/cpp`
- `src/mcp/mcptools_machine.cpp`
- `src/mcp/mcptools_shots.cpp`
- `src/mcp/mcptools_profiles.cpp`
- `src/mcp/mcptools_settings.cpp`
- `src/mcp/mcptools_dialing.cpp`
- `qml/components/McpConfirmDialog.qml`
- `qml/components/layout/items/DiscussItem.qml`
- `resources/icons/discuss.svg`
