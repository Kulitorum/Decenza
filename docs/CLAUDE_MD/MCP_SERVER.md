# MCP Server for Decenza

## Context

Add an MCP (Model Context Protocol) server to Decenza so AI assistants (Claude Desktop, etc.) can fully monitor and control the DE1 espresso machine over the network. The MCP server rides on the existing ShotServer HTTP infrastructure at `/mcp`, reusing TLS, auth, and socket management. The user configures MCP behavior (on/off, access level, confirmation level) in the AI settings tab, which gets reorganized into sections to accommodate the new controls.

## Architecture

### Transport: Streamable HTTP

- **POST /mcp** — JSON-RPC 2.0 requests (initialize, tools/call, resources/read, etc.)
- **GET /mcp** — SSE stream for server-initiated notifications (resource changes)
- **DELETE /mcp** — terminate session
- Session tracked via `Mcp-Session` header (separate from auth cookies)

### File Structure

```
src/mcp/
  mcpserver.h/cpp           — Session management, JSON-RPC dispatch, SSE
  mcpremoteaccess.h/cpp     — Remote connector: tokenized loopback/LAN listener (see "Remote Access")
  mcpsession.h/cpp          — Per-client state (capabilities, SSE socket, subscriptions, remote flag)
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

## The tool surface is a budget

`tools/list` is sent in full to every client on every connection, and real clients **truncate it
silently**. ChatGPT exposed 87 of the app's 97 tools: `get_flow_calibration` and
`set_flow_calibration` simply did not exist for that user, while `clear_flow_calibration` from
the same trio did. Nothing in the app reported a problem, because nothing in the app had one.

Two causes, both now bounded by `scripts/check_mcp_tool_budget.py` in the per-PR
`text-invariants` job:

**Size.** The listing carried a base64 SVG icon per tool — 216 KB of a ~312 KB payload, measured — and 41 of 97 tools shipped the SAME 2292-byte generic fallback, because
`iconQrcForTool()` mapped a tool's NAME PREFIX to an icon and every `recipe_*`, `bag_*`,
`equipment_*`, `water_vessel_*`, `mqtt_*` and `ai_*` tool missed. Tools no longer emit `icons` at
any protocol version; resources still do (five records, five distinct icons). The check's
no-inline-`data:` rule is what stops it coming back, because nothing about 216 KB of base64 was
visible at the call site — it was one call to a helper.

**Count.** Every feature added tools; none folded them in. Twelve same-noun families merged into
one tool each, 97 → 66.

The four limits — 80 tools, 500 chars per tool description, 120 per property description, 85 KB
estimated payload — live in `LIMITS` at the top of that script, so tightening them is one edit.

### The server version identifies the SURFACE, not the build

`initialize` returns `serverInfo.version` — `McpSurfaceVersion` in `src/mcp/mcpserver.h` — plus
`serverInfo.appVersion`, which carries the build. They are separate facts and neither substitutes
for the other: **2.0.2 shipped both a 97-tool server and a 66-tool one**, so a version that
followed the app version would have read identically across the change that halved the list.

**Bump `McpSurfaceVersion` whenever the tool surface changes** — a tool added, removed or renamed,
an action added to a merged tool, an argument whose meaning changes. `check_mcp_tool_budget.py`
fingerprints the registered tools and their actions and fails the PR when the surface moves
without the version moving, printing the fingerprint to paste into `McpSurfaceFingerprint`. So the
rule is enforced rather than remembered.

**Why it must be right, separately from what clients do with it.** `initialize` is where the
server states what it is. A server whose surface has changed while its version has not is putting
a false statement on the wire, and a client that keys on the version — today, or under the
2026-07-28 caching semantics — can only behave correctly if we tell the truth. Pinning `1.0.0`
forever defeated exactly the clients that would have done the right thing.

**What it does not do on its own is refresh an existing cache.** Clients cache the catalogue they
fetched at `initialize` and refresh only on reconnect — and some not even then. Reported,
repeatedly, and not by us:

- Claude Code caches by SERVER NAME and never invalidates; a server that went 5 → 15 tools kept
  showing 5 until the entry was renamed in `.mcp.json`
  ([claude-code#40025](https://github.com/anthropics/claude-code/issues/40025)).
- claude.ai keeps the list through disconnect/reconnect and even delete-and-re-add
  ([claude-ai-mcp#137](https://github.com/anthropics/claude-ai-mcp/issues/137),
  [claude-code#38324](https://github.com/anthropics/claude-code/issues/38324)).
- Codex fails to invalidate on an explicit `tools/list_changed`
  ([codex#33266](https://github.com/openai/codex/issues/33266)).

Observed here: after this app dropped to 66 tools, ChatGPT still reported 97 across a manual
disconnect and reconnect.

This server declares no `tools.listChanged` and should not: tools are registered once at startup
and never change while the app runs, so the notification could never fire. The version's job is
narrower and still worth having — it makes a stale session **visible**, which is otherwise only
inferable by counting tools.

The actual fix is protocol-level. MCP 2026-07-28 adds `ttlMs` and `cacheScope` to list results
precisely because clients cache catalogues; adopting it here is real work (that revision also
makes the core stateless, and this server is session-based with held responses for in-app
confirmation), and whether the clients honour the hints is unverified.

### Adding a tool

1. **Does it belong to a noun that already has a tool?** Add an action to that tool, not a new
   one. One family, one name.
2. **Declare a tier** — `McpTierCore`, `McpTierStandard` (default) or `McpTierNiche`.
   `listTools()` emits in `(tier, name)` order, so a client that truncates loses the niche tail
   first, and the same build always presents the same order. It used to emit in `QHash` order,
   which is why WHICH tools went missing was arbitrary and changed between runs.
3. **Keep the description under 500 characters** — what a client needs to CHOOSE the tool and
   fill its arguments. Everything else (output fields, worked sequences, interaction rules) goes
   in `resources/ai/tools/<topic>.md`, is listed in `resources/ai.qrc`, and is served by
   `get_agent_file(topic)` and `decenza://tools/<topic>`. Both surfaces enumerate the resource
   directory (`src/mcp/mcpagentdocs.h`), so there is no topic list in code to update.

### Merged tools: `action` dispatch

`registerActionTool(name, description, schema, actions, tier)` takes a `QVector<McpToolAction>`
built with `McpRegistryHelpers::syncAction` / `asyncAction`. Each action declares its own
`category` and, when it needs confirmation, the wording for the dialog — confirmation is implied
by supplying that wording, so there is no bool that can disagree with the text beside it.

- The `action` property is **injected by the registry**, with its enum built from the actions
  themselves, so the schema a client validates against cannot drift from what dispatch accepts.
- `action` is **required and has no default**. A default would let an omitted argument resolve to
  some behaviour, and the destructive verbs are exactly where that is worst.
- Dispatch is uniformly **async** even when every verb is synchronous, so a family that mixes the
  two (`auto_load` reads sync, writes async) needs no special case. A sync verb responds inline.
- `categoryFor(tool, args)` and `confirmationFor(tool, args)` **fail closed**: an `action` the
  registry cannot resolve is gated as the tool's strictest verb, and confirmed if ANY verb would
  confirm. The handler's "valid actions are…" error comes after that gate, not instead of it.
- Handlers still **never** inspect `confirmed` — the server strips it before dispatch. A
  handler-side check is unreachable-true and was the shipped #1219 bug.

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

Each tool has a `category` that determines the minimum access level required. A **merged
tool declares a category per action**, so `bag` action=list is `read` while action=update is
`settings`; the resolution is `McpToolRegistry::categoryFor(tool, args)` and it is what both the
access check and the rate limiter consult.

| Category | Min Access Level | Tools (merged tools listed by verb where the verbs differ) |
|----------|-----------------|-------|
| `read` | 0 (Monitor) | machine_get_state, app_get_info, machine_get_telemetry, shots_list, shots_get_detail, shots_get_debug_log, shots_compare, profiles_list, profiles_get_active, profiles_get_detail, profiles_get_params, settings_get, get_agent_file, dialing_get_context, dialing_get_grinder_calibration, steam_get_health, recipe_list, recipe_get, `ai_conversations` (all), `auto_load` get, `bag` list, `equipment` list, `flow_calibration` get, `steam_pitcher` list, `water_vessel` list, `devices_wifi` results |
| `control` | 1 (Control) | machine_wake, machine_sleep, machine_stop, machine_skip_frame, `machine_start` (all), `scale_timer` (all), scale_tare, shots_update, shots_upload_to_visualizer, backup_now, `mqtt` (all), devices_connect_de1, devices_disconnect_scale, devices_reset_scale_priority, bag_extract_details, `bag` select, `equipment` select, `steam_pitcher` select, `water_vessel` select, `devices_wifi` browse |
| `settings` | 2 (Full) | profiles_set_active, profiles_edit_params, profiles_save, profiles_delete, profiles_create, profiles_rename, shots_delete, settings_set, apply_theme, `reset_saw_learning` (all), recipe_create, recipe_update, recipe_create_from_shot, recipe_clone, recipe_archive, `auto_load` set/clear, `bag` create/update, `equipment` update/merge, `flow_calibration` set/clear, `steam_pitcher` add/update/delete, `water_vessel` add/update/delete |

**An `action` the server cannot resolve is gated as the tool's STRICTEST verb**, not as its
most permissive one. Omitting `action` on `bag` is therefore refused at Monitor level and
confirmed like a destructive write, and only then does the handler return its "valid actions
are…" error. That ordering is the point: the alternative lets a caller drop an argument to be
gated as a read.

### Tool → Confirmation Level Mapping

Two confirmation mechanisms are used depending on where the user is:

- **In-app dialog**: For machine start operations — the user is physically at the machine and must approve on screen
- **Chat-based**: For settings/data operations — the user is at their desk interacting with the AI remotely

| Tool | Dangerous Only (1) | All Control (2) | Mechanism |
|------|-------------------|-----------------|-----------|
| machine_start (any action) | **Confirm** | Confirm | In-app dialog |
| machine_wake/sleep | No confirm | Confirm | Chat |
| machine_stop | No confirm | Confirm | Chat |
| machine_skip_frame | No confirm | Confirm | Chat |
| profiles_set_active | **Confirm** | Confirm | Chat |
| profiles_edit_params | **Confirm** | Confirm | Chat |
| profiles_save | **Confirm** | Confirm | Chat |
| profiles_delete | **Confirm** | Confirm | Chat |
| profiles_create | **Confirm** | Confirm | Chat |
| profiles_rename | **Confirm** | Confirm | Chat |
| shots_delete | **Confirm** | Confirm | Chat |
| settings_set | **Confirm** | Confirm | Chat |
| reset_saw_learning (both actions) | **Confirm** | Confirm | Chat |
| flow_calibration set / clear | **Confirm** | Confirm | Chat |
| equipment merge | **Confirm** | Confirm | Chat |
| devices_set_scale_priority_mode | **Confirm** | Confirm | Chat |
| devices_reset_scale_priority | **Confirm** | Confirm | Chat |
| devices_disconnect_scale | **Confirm** | Confirm | Chat |
| shots_update | No confirm | No confirm | — |
| shots_upload_to_visualizer | No confirm | No confirm | — |

When confirmation level is 0 (None), all tools execute immediately regardless of mechanism.

### In-App Confirmation (the machine_start tool)

For operations that physically affect the machine, confirmation happens on the device screen where the user can see and control the machine:

1. `McpServer::handleToolsCall()` detects the tool needs in-app confirmation
2. The HTTP response is **held** — stored in `PendingConfirmation` with a `QPointer<QTcpSocket>`
3. McpServer emits `confirmationRequested(actionId, toolDescription, sessionId)` — `actionId` is `machine_start.espresso` for a merged tool, so the dialog names the verb rather than the family
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
2. If not confirmed, returns immediately with: `{"needs_confirmation": true, "action": "settings_set", "description": "Change machine settings", "parameters": {...}}`. For a merged tool the `action` field carries `<tool>.<verb>`, e.g. `steam_pitcher.delete`
3. The AI sees this response and asks the user in chat: "I'd like to change the brew temperature to 94°C. Should I proceed?"
4. If the user approves, the AI re-calls the tool with `"confirmed": true` in the arguments
5. The tool executes normally (the `confirmed` key is stripped before passing to the handler)

This avoids holding HTTP connections and works naturally with the conversational AI flow. The `confirmed` parameter is declared in each tool's `inputSchema` so the AI knows to include it.

## Remote Access (Mobile Connectors)

The LAN transport above only works for clients that can reach the tablet's LAN
IP (Claude Desktop via `mcp-remote`, MCP Inspector, curl). **Claude and ChatGPT
mobile "custom connectors" dial the MCP endpoint from the vendor's cloud
backend, not from the phone**, so the endpoint must be reachable on the public
internet over HTTPS. `McpRemoteAccess` (`src/mcp/mcpremoteaccess.{h,cpp}`)
provides that. Added by the `add-remote-mcp-connector` change; **opt-in,
defaults off**.

### Capability-URL authentication

A remote request is authorized by an unguessable path segment:

```
https://<host>/mcp/<token>        token = 128-bit CSPRNG, base64url (22 chars)
```

- Wrong or missing token → **bare `404`** (no body — indistinguishable from "no
  server here"). Comparison is constant-time; failed attempts are rate-limited
  per source (defense-in-depth + log hygiene; the attempted path is never
  echoed to the log).
- **Rotation is revocation.** Settings → *Rotate token* generates a new URL and
  immediately drops every live remote connection, so the old URL dies at once.
  The user must then update the connector on claude.ai.
- claude.ai custom connectors treat OAuth as optional — a server that never
  returns a `401` challenge connects unauthenticated. So there is **no OAuth**;
  the single principal is whoever holds the capability URL. Trade-off (token in
  URL lands in logs/proxies) is accepted for this single-owner threat model and
  bounded by the access-level + confirmation gates below.

### Isolated surface

The connector terminates at a **dedicated listener owned by `McpRemoteAccess`**,
separate from ShotServer. It serves **only** `POST/GET/DELETE /mcp/<token>` and
returns a bare `404` for everything else — ShotServer's web editor, REST API,
and data-migration endpoints are never exposed publicly, and a future ShotServer
route can't leak into the remote surface. Matching requests are forwarded
in-process to the same `McpServer::handleHttpRequest` dispatch the LAN path uses,
with the session flagged remote (`McpSession::isRemote()` — informational; for
status UI/logging only).

### Access control is unchanged

`mcpAccessLevel` and `mcpConfirmationLevel` apply to remote sessions **exactly**
as to LAN sessions — same tool-dispatch gates, same in-app confirmation dialog
for machine-start operations. There are no remote-only bypasses. Remote sessions
count toward the same session and rate limits.

### Reachability modes (Settings → AI → MCP → Remote Access)

| Mode | Status | How it reaches the public internet |
|---|---|---|
| **Custom URL (BYO)** | **Shipped (Phase 1)** | The user runs a reverse proxy / tunnel on any box (Tailscale Funnel, cloudflared named tunnel, nginx, …) that forwards to the tablet's LAN IP + the remote port. The listener binds a routable interface so an off-box proxy can reach it. |
| Embedded Tailscale (tsnet + Funnel) | Planned | gomobile-embedded `tsnet` joins the user's tailnet and `ListenFunnel()`s a stable `https://<node>.<tailnet>.ts.net` URL; forwards to a loopback listener. Needs a Go toolchain / AAR + CI work. |
| Embedded ngrok | Deferred | `ngrok-java` agent SDK; pending an interstitial-compatibility spike. |

### Settings (in `SettingsMcp`, addressed as `Settings.mcp.*`)

`remoteMcpEnabled` (default false), `remoteMcpMode` (`custom`|`tailscale`|
`ngrok`), `remoteMcpPort` (default 8890), `remoteMcpCustomBaseUrl`, and the
`remoteMcpToken` (128-bit, generated lazily on first read, stored in QSettings
alongside the existing `mcpApiKey`). `RemoteMcpAccess` is a QML context property
exposing `status`/`statusString`/`statusDetail`/`connectorUrl`/`listenPort` and
the `refresh()` / `rotateToken()` invokables.

### Limitations

- No wake-from-doze: the tunnel/listener lives in the app process. Decenza
  tablets are typically plugged in and screen-on, so this is a corner case, but
  if Android kills connectivity the connector fails closed (vendor backend gets
  a timeout). No FCM wake-up is possible without a relay.
- Depends on the tunnel vendor's TLS/edge for the public hop.

## Tools (Full Set)

### Machine Control
| Tool | Description | Category |
|------|-------------|----------|
| `machine_wake` | Wake from sleep | control |
| `machine_sleep` | Put to sleep | control |
| `machine_start` | Start an operation: `action` = `espresso` \| `steam` \| `hot_water` \| `flush`. `action=espresso` takes optional brew overrides (dose, yield, temperature, grind, rpm) that apply to this shot only, matching QML BrewDialog. Every action raises the in-app dialog. | control |
| `machine_stop` | Stop current operation | control |
| `machine_skip_frame` | Skip to next profile frame | control |

### Machine State
| Tool | Description | Category |
|------|-------------|----------|
| `machine_get_state` | Phase, connection, readiness, heating status, water level (ml + mm), firmware version, active profile name. Platform/OS info has moved to `app_get_info` (#988) to keep tight polling responses small. | read |
| `app_get_info` | App and device info: appVersion, **buildNumber**, qtVersion, OS, kernel, architecture, deviceModel, screen size/DPI, devicePixelRatio. Diagnostics-grade — call once per session. `buildNumber` (`versionCode()`) is what identifies a build: one `appVersion` can ship many, since pre-releases are re-cut against a rolling version — v2.0.4 shipped as 3552, 3553 and 3554. Ask for it before concluding whether a fix is present in a field report. | read |
| `machine_get_telemetry` | Live pressure, flow, temp, weight, goal values. During a shot, also returns the current shot's time-series data so far (not just the latest sample) so the AI can detect channeling or stalling mid-shot. | read |
| `steam_get_health` | Detailed steam-system health: baseline + current pressure/temperature, flow-restriction progress toward warn thresholds, status, and recommendation. Used for steam-wand cleaning / descaling guidance. | read |

### Shot History
| Tool | Description | Category |
|------|-------------|----------|
| `shots_list` | List shots with filters (limit, offset, profile, bean, enjoyment, after/before date range) | read |
| `shots_get_detail` | Full shot record with time-series data | read |
| `shots_get_debug_log` | Per-shot debug log (BLE frames, phase transitions, SAW events, flow calibration). Paginated with offset/limit. `filter` (substring, or regex when `regex` is true; case-insensitive) narrows which lines qualify before pagination; `dedupe` collapses consecutive qualifying lines that are identical apart from any leading timestamp into one entry carrying `count`/`lastLine` (non-consecutive repeats stay separate); `tail` (last N qualifying/deduped entries) takes precedence over `offset` when both are given. `minLevel` is accepted but has no effect — shot debug log lines aren't level-tagged. Every returned line carries its absolute line number in a `lines` array alongside the existing `log` string. | read |
| `shots_compare` | Side-by-side comparison of 2+ shots with auto-computed change diffs (grind, dose, yield, duration) | read |
| `shots_update` | Update any metadata field on a shot: enjoyment, notes, dose, yield, bean info, grinder info, barista, TDS, EY. Same fields the QML shot editor can change. Replaces the old `shots_set_feedback`. If the shot already has a `visualizer_id` and `visualizerAutoUpdate` is on, the edits are auto-PATCHed up to visualizer.coffee (response includes `visualizerUpdateTriggered`). | control |
| `shots_upload_to_visualizer` | Upload a historical shot to visualizer.coffee for the first time (POST). Use for shots that were never auto-uploaded and therefore have no `visualizer_id` yet. Refuses to re-upload an existing shot (points the caller at `shots_update` to PATCH instead) and rejects upfront if the shot is a maintenance profile, shorter than `visualizerMinDuration`, or credentials are missing. Response: `{success, uploadTriggered, message}`; the new `visualizer_id` lands in the local DB when the network response arrives. | control |
| `shots_delete` | Delete a shot by ID. Permanent and cannot be undone. | settings |

`shots_get_detail` also surfaces the shot's coffee bag snapshot (bean-bag-inventory): sparse-emitted `bagId`, `frozenDate`, `defrostDate` (ISO dates; pre-bag shots and unfrozen beans omit them).

### Coffee Bags (bean-bag-inventory)
| Tool | Description | Category |
|------|-------------|----------|
| `bag` action=`list` | List coffee bags (inventory by default; `includeEmpty=true` adds bags marked empty). Each bag carries identity, `kind` ("coffee"/"tea", creation-time), freeze lifecycle, last-used grinder/dose, a parsed `beanBase` snapshot, `isActive`, and — tea bags — the structured brewing fields (teaType, brewTemperatureC, leafGramsPer100Ml, steepTime). | read |
| `bag` action=`select` | Set the active bag — what the next shot is pulled with (applies bean identity + last-used grinder/dose). `bagId: 0` clears the selection. | control |
| `bag_extract_details` | Run the "Get info from page" AI extraction for a bag's URL and return the fields WITHOUT writing (`bag` action=update applies them). Reports stage (1 local fetch / 2 provider web-fetch fallback) + provider/model. Consumes provider tokens. Stays a separate tool: parsing a photographed label is a different job that happens to share the noun. | control |
| `bag` action=`create` | Create an inventory bag; `kind` = coffee (default) \| tea, stamped at creation and immutable — the MCP counterpart of Add Coffee / Add Tea. Tea vocabulary only on tea bags; roastLevel/grinderSetting only on coffee. NOT auto-activated (use action=select). | settings |
| `bag` action=`update` | Update bag fields (metadata + freeze lifecycle + bean/tea details). Partial: only provided keys change; `""` clears a text/date field. `inInventory=false` = "Bag finished"; setting `defrostDate` records a thaw. `kind` is NOT editable; tea vocabulary is rejected on coffee bags. | settings |

### Equipment Packages (add-equipment-packages)
The grinder is a first-class, switchable **equipment package** (the active bag points at one via `equipment_id`). The grind setting + `rpm` stay as per-bag dial-in.
| Tool | Description | Category |
|------|-------------|----------|
| `equipment` action=`list` | List equipment packages. Each carries `id`, `name`, `grinderBrand/Model/Burrs`, `rpmAdjustable`, `inInventory`, last dial (`lastGrindSetting`/`lastRpm`), `shotCount`, and `isActive`. | read |
| `equipment` action=`select` | Set the active equipment package — the grinder the next shot is ground on. Applies the package's grinder identity + last grind/rpm and points the active bag at it. | control |
| `equipment` action=`update` | Edit a package's grinder identity (`grinderBrand/Model/Burrs`) and/or `name`. Partial; re-derives `rpmAdjustable`. Reference semantics (applies to all referencing bags/shots). CHANGING a component on a package that has shots forks a new package (returned `package.id` differs); filling in a component that was EMPTY is enrichment and edits in place. | settings |
| `equipment` action=`merge` | Fold `sourcePackageId` into `targetPackageId`: shots, bags and recipes move to the target, the target returns to inventory, the source is deleted. The repair for a grinder wrongly split in two. Destructive and not undoable — the user names both packages. | settings |

The `de1://dialing` resource's grinder block also exposes `packageId`, `rpmAdjustable`, and `rpm`.

### Recipes (add-recipes)
A recipe is the whole drink: profile + bean link + equipment + dose/yield/temp + the recipe's own grind + steam block. The temperature field is `tempOffsetC` — a SIGNED delta in °C against the recipe's profile (present only when non-zero; 0/omitted = brew at the profile's temperature). There is no absolute recipe-temperature field: the old `temperatureOverrideC` was removed in recipe-relative-temp-offset, and a create/update sending it is rejected with an error naming the replacement (never silently dropped — an absolute written into the delta field would corrupt the temperature). Grind always lives on the recipe (fix-recipe-grind-integrity): responses expose it as `grind: {value, rpm}` (the key is omitted when the recipe has no grind) with no inherited/pinned mode — nothing resolves from the bag. A create that links a bag but *omits* `grindPinned` adopts the bag's current dial at save time (an explicitly empty string stores no grind). Mutations run through the app's RecipeStorage so the UI refreshes like a local edit; `recipe_activate` uses MainController's single activation path (identical to the idle pill tap).
| Tool | Description | Category |
|------|-------------|----------|
| `recipe_list` | List recipes (MRU order, `isActive` marks the machine's current setup; `includeArchived=true` adds archived ones). Each recipe carries `drinkType` (stored, or derived for legacy rows). | read |
| `recipe_get` | One recipe fully resolved: its own grind, the linked bag's identity/status, steam block. | read |
| `recipe_create` | Create from explicit fields; only `name` always required — `profileTitle` is required unless the payload carries a hot-water block with `hasWater` true (profile-less hot-water tea). Accepts `drinkType` (derived from blocks when omitted). | settings |
| `recipe_update` | Partial update; `grindPinned` is the recipe's own grind (`""` clears it). Block/profile changes re-derive `drinkType` unless set in the same call; clearing `profileTitle` requires an active hot-water block in the same call. | settings |
| `recipe_create_from_shot` | The promotion path: prefills from a shot record + its steam snapshot (fallback: current steam settings); the shot's own recorded grind/rpm become the recipe's grind. | settings |
| `recipe_clone` | Copy + rename (family-variant workflow); provenance points at the source recipe. | settings |
| `recipe_archive` | Archive (used recipes can never be hard-deleted — same rule as bags); `restore=true` unarchives, `delete=true` hard-deletes an unused recipe. | settings |
| `recipe_activate` | Apply the whole drink: profile, the linked bag (a bean-less recipe clears the active bag), equipment, dose/yield/temp, the recipe's own grind, steam (+ heater warm-up when `hasMilk`). | control |

### Profile Management
| Tool | Description | Category |
|------|-------------|----------|
| `profiles_list` | List all available profiles | read |
| `profiles_get_active` | Get current profile name + details | read |
| `profiles_get_detail` | Full profile JSON by filename | read |
| `profiles_get_params` | Get the current profile's editable recipe parameters, tailored to its editor type (dflow, aflow, pressure, flow). Returns all parameters that can be passed to `profiles_edit_params`. Always reports `recommendedDoseG` **together with** `hasRecommendedDose` — every profile holds a dose whether one was set or not (the default is 18 g), so a bare figure would read as a recommendation that does not exist. | read |
| `profiles_set_active` | Load and activate a profile | settings |
| `profiles_edit_params` | Edit the current profile's recipe parameters and regenerate frames. Only provide fields you want to change — unspecified fields keep their current values. Triggers frame regeneration and uploads to machine. Profile is marked modified but not saved to disk — call `profiles_save` to persist. `dose` sets the profile's `recommended_dose` and enables it (clamped 0–100 g, must be a number, 0 clears it); it is handled for every editor type, advanced included, before the unrecognised-key check. `recommended_dose` / `has_recommended_dose` are retired and reported in `retiredFields`. | settings |
| `profiles_save` | Save the current (modified) profile to disk. Without this, edits are active on the machine but lost if another profile is loaded. Optionally provide filename + title for Save As. | settings |
| `profiles_delete` | Delete a user/downloaded profile. For built-in profiles, removes local overrides and reverts to the original built-in version. | settings |
| `profiles_create` | Create a new blank profile with a given editor type (dflow, aflow, pressure, flow, advanced) and title. Uses the same creation functions as the QML UI. | settings |
| `profiles_rename` | Rename a user/downloaded profile in place — changes only the display title, keeps the filename so favorites/auto-load/selected references stay valid. Built-in profiles are read-only and rejected (use `profiles_save` Save As to copy). | settings |

The auto-load pin (profile OR recipe) is the `auto_load` tool — see its own row in Settings below.

### Settings
| Tool | Description | Category |
|------|-------------|----------|
| `settings_get` | Read all app settings, specific keys, or a category. Categories: machine, calibration, connections, screensaver, accessibility, ai, espresso, steam, water, flush, dye, mqtt, themes, visualizer, update, data, history, language, debug, battery, heater, autofavorites. The `ai` category includes `aiProvider`, the effective `aiModel` + `aiModelDisplay` for the active provider, and `aiAvailableModels` (the catalog of selectable `{id,name}` for that provider — use it to discover valid `aiModel` values). Sensitive fields (API keys, passwords) are excluded. | read |
| `settings_set` | Update any app setting across all QML Settings tabs. Covers 100+ fields: machine, calibration, connections, screensaver, accessibility, AI, espresso, steam, water, flush, DYE, MQTT, themes, visualizer, update, data, history, language, debug, battery, heater, auto-favorites. `aiProvider` selects the provider; `aiModel` selects the model for the active (or same-call) provider, validated against that provider's catalog (invalid ids rejected with `validModels`); OpenRouter/Ollama use `openrouterModel`/`ollamaModel`. Sensitive fields (API keys, passwords) excluded. | settings |
| `reset_saw_learning` action=`all` | Erase ALL stop-at-weight learning: global pool, every per-(profile, scale) history and pending batch, and the bootstrap. | settings |
| `reset_saw_learning` action=`profile` | Erase one (profile, scale) pair only. Defaults to the active profile and the scale currently serving shots. Kept as a VERB rather than an optional argument on the global form: an omitted argument that means "wipe everything" is the wrong default for an irreversible tool. | settings |
| `flow_calibration` action=`get` | Describe a profile's flow calibration: stored per-profile multiplier, global fallback, which one is in effect (`effectiveSource`), the auto-calibration switch, batch progress (`pendingAutoCalShots` of `autoCalBatchSize`), and a plain-language `state` sentence. `allProfiles=true` lists every calibrated profile with `profileExists` — the only way to answer "which profiles are calibrated?", and the only place an orphan entry is visible. | read |
| `flow_calibration` action=`set` | Set the multiplier by hand. Range 0.5-2.7, refused (not clamped) outside it. Reaches a connected machine immediately. With auto calibration ON it is the new starting point and future shots keep adjusting it; with auto OFF it is stored but inert and the result says so in `warning`. | settings |
| `flow_calibration` action=`clear` | Clear the per-profile multiplier. Unlike get/set it accepts a profile that no longer exists, so orphan entries can be removed; `hadCalibration` reports whether anything was stored. | settings |
| `auto_load` action=`get`/`set`/`clear`, target=`profile`/`recipe` | The auto-load pin: what reloads on app start, DE1 wake, and after `revertMinutes` idle on the Idle page. `target` is required. The profile and recipe pins are mutually exclusive — setting one clears the other, which is why they are one tool. `clear` leaves `revertMinutes` untouched. | read (get) / settings (set, clear) |
| `apply_theme` | Apply a preset theme ('Default Dark', 'Default Light', or user-created). | settings |
| `backup_now` | Create an immediate backup of database, settings, profiles, and media. | control |
| `mqtt` action=`connect`/`disconnect`/`publish_discovery` | The Home Assistant MQTT bridge. `publish_discovery` requires a live connection; `disconnect` on an already-disconnected broker is a success carrying `alreadyDisconnected: true`, not an error. | control |

### Steam & Hot Water Presets
Two tools, `steam_pitcher` and `water_vessel`, each with `action` = `list` \| `add` \| `update` \| `delete` \| `select`. Presets carry their own duration/flow/volume and a **per-preset temperature**. `select` switches the active preset and applies it to the machine (like `bag`/`equipment` select); `add` appends and selects; `update` is partial, and editing the selected preset re-applies it. Units are human-readable (temperatureC, flowMlPerSec, durationSec, volumeMl) — the store keeps steam flow in hundredths and water flow in tenths of mL/s, converted in the tool layer.

The five `steam_pitcher_*` tools this replaced each declared a `confirmed` argument that nothing enforced — none was ever in McpServer's confirmation list. The argument is gone rather than honoured: these are small, re-creatable presets, and the confirmation net is worth more unspent on routine edits.
| Tool | Description | Category |
|------|-------------|----------|
| `steam_pitcher` action=`list` | Presets (name, durationSec, flowMlPerSec, temperatureC; pitcherWeightG/calibMilkG when calibrated; disabled "Off" presets carry only name+disabled) plus `selectedIndex`. | read |
| `steam_pitcher` action=`add` | Add a preset and select it. Optional durationSec/flowMlPerSec/temperatureC (temperature defaults to the global steam temperature); `disabled=true` adds an "Off" preset that turns the heater off. | settings |
| `steam_pitcher` action=`update` | Update by index — partial; unspecified fields keep their values. Editing the selected pitcher re-applies it. Disabled presets can't be edited (delete + re-add). | settings |
| `steam_pitcher` action=`delete` | Delete a preset by index. | settings |
| `steam_pitcher` action=`select` | Switch the active pitcher by index; applies its duration/flow/temperature to the machine. | control |
| `water_vessel` action=`list` | Presets (name, volumeMl, mode "weight"/"volume", flowMlPerSec, temperatureC) plus `selectedIndex`. | read |
| `water_vessel` action=`add` | Add a preset and select it. Optional volumeMl/mode/flowMlPerSec/temperatureC (temperature defaults to the global hot water temperature). | settings |
| `water_vessel` action=`update` | Update by index — partial. Editing the selected vessel re-applies it. | settings |
| `water_vessel` action=`delete` | Delete a preset by index. | settings |
| `water_vessel` action=`select` | Switch the active vessel by index; applies its volume/mode/flow/temperature to the machine. | control |

### Devices & Scale
| Tool | Description | Category |
|------|-------------|----------|
| `devices_list` | List discovered BLE devices (DE1 machines and scales) | read |
| `devices_scan` | Start scanning for BLE devices | control |
| `devices_connect_scale` | Connect to a scale by BLE address | control |
| `devices_connect_de1` | Connect to a DE1 machine by BLE address | control |
| `devices_disconnect_scale` | Disconnect and forget the current BLE scale | control |
| `devices_connection_status` | Get connection status of DE1 machine and scale, incl. in-memory scale connection-priority (dual-HIGH backoff) state | read |
| `devices_reset_scale_priority` | Clear the in-memory scale connection-priority dual-HIGH backoff latch (re-detects on next scale (re)connect; eventually-consistent) | control |
| `scale_tare` | Tare (zero) the connected scale | control |
| `scale_timer` action=`start`/`stop`/`reset` | The scale's built-in timer. Not every scale supports remote timer control, and some cannot reset independently — their reset also starts the timer, which `action=reset` refuses rather than pretending. | control |
| `scale_get_weight` | Get current weight and flow rate from the scale | read |

### Debug & Agent
| Tool | Description | Category |
|------|-------------|----------|
| `debug_get_log` | Read the persisted app debug log. Three addressing modes: (1) `sessions=true` lists all sessions with index/start line/timestamp/line count (session-boundary index is cached, keyed on the log file's size+mtime, instead of rescanned per call); (2) `session=N` addresses that session (-1 = most recent); (3) default — addresses the whole log. Within modes 2/3: `filter` (substring, or regex when `regex` is true; case-insensitive) and `minLevel` (`DEBUG`/`INFO`/`WARN`/`ERROR`/`FATAL`, combines with `filter` via AND) narrow which lines qualify; `dedupe` then collapses consecutive qualifying lines that are identical apart from each line's own leading timestamp into one entry carrying `count`/`lastLine` (non-consecutive repeats, or lines that differ elsewhere — e.g. different shot ids in an otherwise-identical template — are never collapsed together); `tail` (last N qualifying/deduped entries) takes precedence over `offset` when both are given, ahead of `offset`/`limit` (1–2000 lines) pagination. Omitting `filter`/`minLevel`/`dedupe`/`tail` reproduces the exact original response shape. Every returned line carries its absolute line number in a `lines` array alongside the existing `log` string. | read |
| `get_agent_file` | With no arguments: the current `claude_agent.md` system-prompt content, the app version, and the list of documentation topics. Any MCP client should call this at session start. Clients with filesystem access (Claude Code Remote Control) additionally use the version to self-update a local CLAUDE.md. With `topic`: that tool's long-form documentation from `resources/ai/tools/<topic>.md` — the detail that used to sit in tools/list descriptions. An unknown topic errors with `availableTopics`. | read |

### AI Dial-In Conversation (key feature)

The MCP enables an external AI (e.g. Claude Desktop) to act as a dial-in advisor with full machine context. Unlike the in-app AI which uses a cloud provider and limited context, an MCP-connected AI can use its own capabilities with direct access to machine tools.

| Tool | Description | Category |
|------|-------------|----------|
| `dialing_get_context` | Get full dial-in context bundle: current profile recipe + profile knowledge (includes espresso system prompt, dial-in reference tables, and profile-specific KB) + recent shot summary (via `ShotSummarizer`) + dial-in history (last N shots with the same profile family **and the same equipment package** — grinder, basket and puck prep together) + bean metadata + grinder context (observed settings range and noise-filtered typical `stepSize`). When the package has no prior shots, the response carries `noDialInHistory` — the package named, `matchedShotCount: 0` — instead of omitting the history section. This is the primary read tool for dial-in — a single call gives the AI everything it needs to analyze a shot and suggest changes. The cross-profile grinder calibration table is **not** in this bundle — see `dialing_get_grinder_calibration` (#1164). | read |
| `dialing_get_grinder_calibration` | On-demand cross-profile grinder calibration: per-user recommended grinder setting (rgs) for every KB espresso profile, derived from all-time shot history on the same grinder model + burrs. Returns `fineAnchor` / `coarseAnchor`, `conversionKey`, `calibratedUgsRange`, and a `profiles[]` array (each with `ugs`, `rgs`, `source` ∈ history/derived/extrapolated). Split out of `dialing_get_context` (#1164) because it is a ~33-row table that only matters when the user is weighing a profile switch and is a stable physical property of the grinder — so the AI fetches it once on demand instead of re-receiving it every conversational turn. Returns `{available:false, reason}` when fewer than 2 qualifying anchor profiles exist. Same shared `DialingBlocks::buildGrinderCalibrationBlock()` builder the one-shot in-app advisor / `ai_advisor_invoke` still use inline. | read |
| `ai_conversations` action=`list` | List saved multi-shot AI dialing conversations (in-app advisor + `ai_advisor_invoke` turns both land here), most recently active first. Up to `AIManager::MAX_CONVERSATIONS` (5) are retained, oldest evicted. Each entry: `key`, `label`, `beanBrand`/`beanType`/`profileName`, `equipment` (the package the thread belongs to — one bean and profile can hold several threads, one per equipment package), `messageCount`, `lastUpdated`; `corrupted: true` is added (omitted otherwise) when the entry's stored transcript failed to parse, in which case `messageCount` is unreliable. Same underlying index as the web UI's `/ai-conversations` page. | read |
| `ai_conversations` action=`get` | Get the full transcript for one conversation `key` from action=list: top-level `key` echo, a `metadata` object (`beanBrand`/`beanType`/`profileName`/`equipment`/`lastUpdated`), `systemPrompt`, and `messages[]` — every turn in order (`role`, `content`, optional `shotId`, optional `structuredNext` on assistant turns that made a concrete recommendation). Same QSettings data as the web UI's JSON download, returned as structured JSON. Useful for collecting real conversation transcripts to validate prompt changes (issue #639). | read |
| ~~`dialing_suggest_change`~~ | **Removed.** Was a no-op stub that returned `"suggestion_displayed"` without actually displaying anything or changing settings. The AI mistakenly treated it as applying changes (e.g., grind size). Use `settings_set` to change grind (`dyeGrinderSetting`), dose (`dyeBeanWeight`), yield (`targetWeight`), temperature (`espressoTemperature`), etc. | — |
| ~~`dialing_apply_change`~~ | **Removed.** Was a convenience wrapper that duplicated `settings_set` + `profiles_set_active`. Caused the advanced-profile-corruption bug due to duplicated code paths. Use `settings_set` for temp/weight/DYE changes and `profiles_set_active` for profile switches. | — |

**Design note — why one read tool instead of four:** An earlier version had separate `dialing_get_shot_summary`, `dialing_get_history`, `dialing_get_profile_knowledge`, and `dialing_get_context` tools. These were consolidated into just `dialing_get_context` because: (1) the MCP client almost always needs all the context together, (2) fewer tools means better MCP client compatibility and less confusion for the AI, (3) the individual data is still available via generic tools (`shots_get_detail`, `profiles_get_detail`, `settings_get`) if needed. The `dialing_get_context` tool accepts optional parameters to control what's included (e.g., `shot_id` to analyze a specific shot instead of the most recent, `history_limit` to control how many prior shots to include).

The later `dialing_get_grinder_calibration` split (#1164) does **not** reverse this consolidation: the cross-profile RGS table is not part of "the context the client almost always needs together" — it is a large, stable table that is only relevant during a profile-switch discussion. Keeping it out of the always-on bundle and fetching it once on demand is a payload-cost decision, not a re-split of the shot/history/knowledge bundle.

**Payload-cost discipline (#1164):** the principle is *cache by not resending*. On the MCP path Decenza cannot attach `cache_control` (the external client owns the Anthropic call), so the only lever is "emit stable content once, never resend a different copy" — once it is in the conversation, the external client's prompt cache covers it for free. Three cuts implement this:

1. **`includeFullKnowledge` is now actually gated** (#1164 finding #1). The lite branch of `dialing_get_context` used to append the full cross-profile reference catalog + UGS tables + families/discipline framing (~17 KB) unconditionally for espresso, contradicting the tool's documented contract and dominating per-call token cost. The default now ships only the current profile's KB entry (~1 KB) plus a compact guardrail that tells the AI to fetch the full reference **once** via `includeFullKnowledge: true` at session start (stable → stays in context, no re-request) and not to quote other-profile setpoints from memory until it has. The in-app advisor path is unaffected — it carries the full system prompt in a `cache_control`-wrapped system block (`aiprovider.cpp`), so it is already cache-optimal.
2. **Grinder calibration → on-demand tool** (#1164 finding #2, above).
3. **`dialInSessions` session-context hoisting extended** (#1164 finding #3). `profileName`, `targetWeightG`, and `temperatureOverrideC` now follow the same hoist discipline as grinder/bean identity and `pourControl` (#1158): when every shot in a session shares the value it is emitted once on `session.context`; a per-shot field appears only when a session genuinely mixes the value. A dial-in session is almost always one profile at one target weight and temperature, so this removes near-total per-shot repetition.

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
| Profile knowledge (system prompt + reference tables + per-profile KB + profile catalog + cross-profile guidance) | `ShotSummarizer::shotAnalysisSystemPrompt()` — shared with in-app AI | `dialing_get_context` |
| Shot data (curves, phases, anomalies) | `ShotSummarizer` | `dialing_get_context` / `shots_get_detail` |
| Dial-in history (last N shots, same profile **and equipment package**) | `ShotHistoryStorage::loadRecentShotsByKbIdStatic()` | `dialing_get_context` |
| Grinder context (observed settings range, noise-filtered `stepSize`, burr-swappable flag) | `ShotHistoryStorage::queryGrinderContext()` + `GrinderAliases` — shared with in-app AI | `dialing_get_context` |
| Grinder calibration (per-user RGS for every KB profile, derived from all-time anchor shots on same grinder+burrs) | `DialingBlocks::buildGrinderCalibrationBlock()` — shared with in-app AI | `dialing_get_grinder_calibration` (on demand); in-app advisor / `ai_advisor_invoke` inline |
| Bean metadata (brand, type, roast, grinder, burrs) | Shot metadata / Settings DYE | `dialing_get_context` |
| Machine telemetry (live pressure/flow/temp) | `MachineState` / `DE1Device` | `machine_get_telemetry` |
| All available profiles | Profile list | `profiles_list` |
| Water level | `DE1Device::waterLevelMl()` / `waterLevelMm()` | `machine_get_state` |

The MCP AI still has advantages over the in-app AI: it's not limited by token budgets or cloud API costs, and it can maintain a long conversation across multiple shots without context trimming. However, as of PR #635/#647, both paths share the same system prompt (including reference tables, profile catalog with cross-profile recommendation guidance, and burr-swappable grinder enrichment), grinder context logic, and profile knowledge — changes to shared components benefit both equally.


### `dose` on `profiles_edit_params`

`dose` used to write `RecipeParams::dose`, which lived in the profile's `recipe` block and was read
by nothing — not by either frame generator, not by any QML binding, and explicitly excluded from
`frameAffectingFieldsEqual`. Both the block and the field are gone.

The parameter is still accepted, and now writes `recommended_dose` + `has_recommended_dose`, which
are consumed by `dialing_get_context`, `profiles_get_detail` and the AI advisor. Details that matter
for anyone changing this:

- The handler runs **before** the loop that checks incoming keys against the editor's current
  parameter map. Left after it, `dose` would land in `ignoredFields` and the response would report
  it IGNORED — the exact outcome keeping the parameter exists to avoid.
- It clamps to `[0, 100]`, replacing the bound that `RecipeParams::clamp()` used to provide.
  `Profile::setRecommendedDose` is a bare assignment. A clamped value is reported back in
  `adjustedFields` / `adjustedNote` rather than echoed as if it had been stored verbatim.
- **`dose` must be a JSON number.** It is the one key read straight as a double instead of going
  through `toVariant()`, and `QJsonValue::toDouble()` answers `0` for anything else — which
  `setCurrentProfileRecommendedDose` reads as "clear the recommendation". A stringified `"18"`
  would therefore have silently deleted the profile's dose, so a non-number is now rejected with
  `success: false` instead of coerced.
- **`0` clears the recommendation** (sets `has_recommended_dose` false); it does not store a
  recommendation of zero grams.

`recommended_dose` / `has_recommended_dose` are **retired from the edit surface** — `dose` is the
one spelling on every editor type, advanced included (`dose-source-precedence`). They are stripped
explicitly rather than merely dropped from the schema, because nothing validates incoming keys
against it and the advanced branch passes the whole map through. A call containing them gets
`retiredFields` plus a note naming `dose`, and the caveat is folded into `message` alongside any
`ignoredFields` — a client reading only `message` must not be told a clean "Profile updated". A call
whose *only* keys were retired spellings returns `success: false` and changes nothing, rather than
dirtying the loaded profile and inviting a `profiles_save`.

On the read side `profiles_get_params` reports the field once, as `recommendedDoseG` +
`hasRecommendedDose`, on every editor type. The advanced branch's profile-JSON spread drops the
snake_case pair so the response never shows four keys for two fields.

## AI-Friendly Data Conventions

MCP tool responses are consumed by LLMs (Claude, ChatGPT, etc.) which cannot reliably interpret raw numbers without context. All numeric fields in MCP responses follow these conventions:

- **Timestamps**: ISO 8601 with timezone offset (e.g., `"2026-03-21T11:20:41-06:00"`), never Unix epoch
- **Units in field names**: Numeric fields include their unit as a suffix — `doseG` (grams), `pressureBar` (bar), `temperatureC` (Celsius), `flowMlPerSec` (ml/s), `durationSec` (seconds), `weightG` (grams), `targetVolumeMl` (ml)
- **Scales in field names**: Bounded values indicate their range — `enjoyment0to100` (0-100 rating)
- **Human-readable enums**: Machine phases, editor types, and states are returned as strings (`"idle"`, `"pouring"`, `"dflow"`), not numeric codes
- **`stoppedBy` marks whether a yield is a real outcome** (#1161): shots in `dialing_get_context` (`dialInSessions[].shots[]`, `bestRecentShot`) and `shots_list` carry a sparse `stoppedBy` ∈ `"weight"` (stop-at-weight) / `"volume"` (stop-at-volume) / `"manual"` (user tapped Stop). It is **omitted** when the profile ran to completion or the DE1's own button was used (the BLE protocol can't distinguish those) — the AI then falls back to `yieldG` vs `targetWeightG`. A `"manual"` shot's yield/ratio/duration are user-chosen, not extraction outcomes, so the shared `shotAnalysisSystemPrompt` instructs the AI not to diagnose grind/ratio from them; an absent `stoppedBy` with `yieldG` well below `targetWeightG` is treated the same. Classified in `MainController::onShotEnded` (SAW/SAV C++ state is ground truth; QML's resolved `stopReason` supplies "manual" via `reportShotStopReason`), persisted in `shots.stopped_by` (migration 17).
- **Shot identity in prose is date/time, never the numeric `id`** (#1162): the per-shot `id` is an internal DB primary key with no user-facing counterpart — Shot History and every user surface key shots by date/time. The AI must cite shots to the user by their local `timestamp` ("your May 10, 9:04 AM shot"), using `id` only as an opaque argument to other tools. This rule is delivered to MCP clients via the server-level `instructions` string in the `initialize` result (retained for the whole session, beverage-agnostic, zero per-call cost) and reinforced in the shared `shotAnalysisSystemPrompt` (`## How to Read Structured Fields`). Two carriers because the full system prompt only reaches MCP clients that call `ai_advisor_invoke` (always carries it) or `dialing_get_context` with `includeFullKnowledge: true` (opt-in since #1164); for a client that does neither, the handshake `instructions` is the only carrier. The `instructions` field is emitted unconditionally, and the gate it used to carry rested on a false premise: `schema/2024-11-05/schema.ts` already declares `instructions?: string;` on `InitializeResult`, with the same docblock as every later revision. It was optional and present from the first revision, so it was never version-sensitive.

When adding new MCP tool responses, never return raw numbers that require domain knowledge to interpret. An AI seeing `"pressure": 9.0` doesn't know if that's bar, PSI, or kPa. Use `"pressureBar": 9.0` instead.

### `error` is a reserved key: it marks the tool call FAILED

**A tool reports failure by returning a top-level `error` key in its result object.** `buildToolCallResponse()` sees the tool's own `error` key on the payload it is wrapping and sets `isError: true` on the envelope. Every tool inherits this; a new tool needs no opt-in, and no call site may hand-roll the marking (the confirmation-denial path used to, and no longer does — one place decides what a failed tool call looks like). ~291 sites across `src/mcp/mcptools_*.cpp` use this shape and none uses a different spelling — measure with `grep -rhoE '\["error"\] *=|\{"error"' src/mcp/mcptools_*.cpp | wc -l` rather than trusting this number, which is a hostage to the next tool file.

**What is NOT marked: a failure signalled any other way.** The rule is "an `error` key is marked", not "every failure is marked". A payload carrying `success: false` with no `error`, a `warning`, an `available: false`, an empty result standing in for "unavailable", or a failure reported through the registry's `errorOut` out-parameter is invisible to this mechanism. If your failure does not put a top-level `error` in the payload, it ships as a successful call. Either give it one, or be sure it is a deliberate non-failure — several payloads here legitimately report a partial outcome on a successful call (`set_flow_calibration`'s `warning`, `profiles_edit_params`' `ignoredFields`, `dialing_get_grinder_calibration`'s `available: false`), and each says so at its site.

The consequence is that **`error` cannot be used as an ordinary data field in a tool payload.** A tool wanting to report an error-shaped value that is not a failure has to name the field something else — `bag_extract_details` is the worked example: a stage-1 failure inside an otherwise-successful call is named `stage1Error`, not `error` (`src/mcp/mcptools_ai.cpp`).

The error TEXT stays in `content[]`, which is what the model reads. `isError` is sparse — absent means success, and a successful call carries no `isError` key at all, never `isError: false`. Both follow MCP's `CallToolResult` (`isError?: boolean`, "If not set, this is assumed to be false"; schema 2025-11-25).

**A tool failure stays a JSON-RPC `result`. Never reach for `sendJsonRpcError()` from a tool.** MCP is explicit: errors originating from the tool "SHOULD be reported inside the result object, with `isError` set to true, _not_ as an MCP protocol-level error response. Otherwise, the LLM would not be able to see that an error occurred and self-correct" (schema 2025-11-25, `CallToolResult.isError`) — a JSON-RPC error delivers no `content[]`, so the error text never reaches the model.

JSON-RPC `error` is for protocol faults, and they reach the wire two different ways:

- **Directly via `sendJsonRpcError()`**, bypassing `sendJsonRpcResponse()` entirely: parse error, "Too many sessions", "Session not initialized".
- **As a raw `{error: {code, message}}` returned up through `sendJsonRpcResponse()`**: unknown method (`handleJsonRpc`), the `resources/*` handlers, and the `tools/call` faults that happen *before* dispatch — rate limit, async dispatch failure, tool-registry error.

That second group is why `sendJsonRpcResponse()`'s top-level `contains("error")` test exists and is correct. What it cannot see is a **wrapped tool payload**: once a tool has run, `buildToolCallResponse()` has moved its `error` one level down. Note the test is *not* unreachable for `tools/call` — a rate-limited call takes it — only for a payload that has been through the wrap step.

### `success` means the operation happened, not that the tool was called

The companion rule to the one above, and the half `isError` cannot reach. A tool
that writes no `error` key ships as a successful call — so a tool that *assumes*
its work succeeded reports success for something that did not happen, and nothing
on the wire contradicts it. The bullets below name fifteen tools across seven
distinct mechanisms, and every one was correct C++ that no test and no reviewer
had reason to question.

- **Consult the operation; do not assume it.** `profiles_set_active` called a
  `void` `loadProfile()` and reported "Profile activated" for a profile
  `ProfileManager` had **refused**, while the machine went on brewing the
  previous one. `apply_theme` did the same for a theme name matching nothing.
  Both underlying calls now return `bool`. When an app-layer call cannot report
  its own outcome, **give it a way to** — do not paper over it in the tool.
- **A database call reports "the statement ran", not "a row changed."**
  `query.exec()` succeeds on an `UPDATE`/`DELETE` whose `WHERE` matches nothing.
  Check `numRowsAffected()`. Do **not** add a `SELECT` pre-check instead: it
  races the write (these run on a background thread) and adds a query to a
  one-query path.
- **An async tool must be wired to every terminal signal, not just the happy
  one.** `shots_delete` waited on `shotDeleted`, which fires only on success, so
  a failed delete produced **no response at all** — the client hung, with no
  error and no timeout anywhere in the `_deferred` path. Storage now emits
  `shotDeleteFinished(id, success, reason)` for both outcomes. Prefer a
  request-specific completion signal over a general error signal: a storage-wide
  `errorOccurred` carries no id, so an unrelated failure would resolve your call.
- **An unavailable dependency is an error, not emptiness.** Returning a bare
  `{}`, a default-constructed payload, or `status: ""` gives the model nothing to
  act on and violates the human-readable-enum convention besides. Keep it
  distinct from a meaningful "no data yet" (`steam_get_health` has both:
  `hasData: false` means no steam sessions, an `error` means no tracker).
- **Name the inputs you dropped.** `shots_compare` silently returned a shorter
  list; it now carries `unresolvedShotIds`, and errors when nothing resolves.
- **A no-op is a success, flagged as a no-op.** `devices_connect_de1` and
  `mqtt_disconnect` returned a bare `message` with neither `success` nor
  `error` — a third state nothing can classify. Both now report `success: true`
  plus `alreadyConnected` / `alreadyDisconnected`, and echo the parameters the
  caller supplied. (`mqtt_publish_discovery` still treats a missing connection as
  an `error`, which is not an inconsistency: it cannot do its job without one.)
- **An empty virtual is not a capability.** `ScaleDevice`'s `startTimer` /
  `stopTimer` / `resetTimer` are virtual with empty default bodies, so every
  scale accepted a timer command and the three `scale_timer_*` tools reported
  success on all of them — including Acaia, whose header says in a comment that
  it has no remote timer control. `supportsTimer()` defaults to **false** so a
  driver whose override is forgotten fails in the safe direction.

Not everything that looks like this is a defect. `settings_set`'s ~117 `void`
setters build their response *before* the setters run, so a clamp or rejection
cannot reach it — real, but with no identified failing key, and the fix is a
90-setter refactor. Left alone deliberately.

### Two protocol eras, one endpoint

The server is **dual-era**: it speaks the handshake-based revisions (`2025-06-18`,
`2025-11-25`) and the handshake-less `2026-07-28` on the same endpoint, with the
era chosen per request.

**Which era a request gets, and how.** The discriminator is
`params._meta["io.modelcontextprotocol/protocolVersion"]`. The 2026-07-28 schema
makes it REQUIRED on every modern request and no legacy revision defines it, so
its presence is decisive on its own. It is deliberately NOT the `Mcp-Method` /
`Mcp-Name` headers, which are also required of a modern POST: requiring those
here would reject a modern request whose proxy stripped them. And it is NOT
`MCP-Protocol-Version`, which legacy has sent since 2025-06-18.

**An ambiguous request is served as legacy**, and that is not a neutral default.
Mis-routing a legacy request to the modern path breaks a client that works today
and gives it no recovery — the handshake that would have negotiated a fallback is
the thing 2026-07-28 removed. Mis-routing a modern request to legacy produces the
error a modern client's own detection is specified to fall back from.

**Version sets are accessors, never inline filters.** `supportedProtocolVersions()`
is everything servable; `legacyProtocolVersions()` and `modernProtocolVersions()`
are the two halves. Use them:

| Question | Accessor |
|---|---|
| What may `initialize` negotiate, or fall back to? | `legacyProtocolVersions()` |
| What may a legacy request's `MCP-Protocol-Version` header name? | `legacyProtocolVersions()` |
| What may a modern request's `_meta` name? | `modernProtocolVersions()` |
| What does `server/discover` advertise? | `modernProtocolVersions()` |

Treating "supported" as one set produced the same bug **four times** during this
work: a legacy request honouring a modern header; a modern request accepted under
a legacy version; `server/discover` advertising legacy versions to clients that
could not use them; and a modern subscription stream receiving legacy's
"everything" broadcast. Each was a hand-rolled filter that drifted from its
sibling. Hence accessors.

**`2025-03-26` is accepted as a header value** though not negotiable — it is what
the spec tells a client to send when it cannot identify the version. Treated as an
absent header: it selects nothing and the session's version stands. A supported
header selects *itself*. Do not collapse those two cases.

### Adding a tool so it works in both eras

Most tools need nothing — dispatch below `handleJsonRpc` is shared, and only the
envelope forks. Four things do differ:

1. **Category decides modern reachability.** `control` and `settings` tools are
   rate-limited per peer address in the modern era (`McpRateWindow`), because a
   stateless request has no session to count against. Read tools are not charged.
2. **Confirmation-gated tools work in both eras.** The gate carries its own
   `confirmationId`, not a session id. If you add one, do not reintroduce a
   session dependency.
3. **A method the modern era removed must be refused BY ERA, not deleted** —
   `ping`, `logging/setLevel`, `resources/subscribe`, `resources/unsubscribe` and
   `initialize` are all still correct for legacy.
4. **Modern results are framed centrally.** `resultType` and `serverInfo` are
   stamped where every modern result passes through, so a handler cannot forget
   them. Do not add them per handler.

**Error codes are era-dependent where the revision changed them.**
Resource-not-found is `-32602` for a modern caller and `-32002` for a legacy one —
both correct for the revision they are sent under.

### The session machinery is NOT deleted by dual-era support

Sessions, the reapers, the ceilings, the tombstone set and the auto-recovery
branch all remain, because **legacy needs every one of them**. The simplification
the modern era offers is only realised the day legacy is dropped.

Stated here so it is not re-litigated each time that code annoys someone: a
modern request creates no session and passes `nullptr`, and that is the whole of
its interaction with the pool.

**When does legacy get dropped?** When no client needs it. The revision's own
feature-lifecycle policy sets a twelve-month minimum deprecation window, and the
clients this server actually sees — `claude-code`, the claude.ai connector,
`codex-mcp-client` — are all legacy today. Not soon, and not a decision to take
because the session code is inconvenient.

### Supported protocol revisions

`supportedProtocolVersions()` (`src/mcp/mcpserver.cpp`) lists **`2025-11-25`
(preferred) and `2025-06-18`**, newest first. A client requesting anything else
is answered with the preferred version at `initialize`.

`2024-11-05` and `2025-03-26` were dropped. Measured from the device log across
50 handshakes — every real client this server has — nothing had ever requested
either: `claude-code` and the claude.ai connector negotiate `2025-11-25`,
`codex-mcp-client` negotiates `2025-06-18`. The protocol's own conformance suite
agrees: it rejects `2024-11-05` as an unknown spec version and has zero scenarios
for `2025-03-26`.

`2025-06-18` stays because Codex is on it. It is the one revision behind current
that a real client here actually uses.

Two consequences worth knowing before touching version-gated code:

- **`2025-03-26` is still ACCEPTED as a header value**, though it is not
  negotiable, and this is a **deliberate deviation from a MUST** — the transport
  spec says an unsupported `MCP-Protocol-Version` MUST get a 400. We accept it
  because it is the value the spec tells a *server* to assume when no header
  arrives, and clients emit it for the same reason; the conformance suite sends
  it on concurrent POSTs after negotiating 2025-11-25. Refusing it turns the
  ecosystem's own fallback into a hard 400. Note the spec defines no
  client-sent sentinel — that reading is ours, inferred from client behaviour.
  Accepting the header does not grant that revision's semantics, batching
  included, and it is logged at DEBUG rather than passing silently.
- **The header-absent assumption is `2025-06-18`, not the `2025-03-26` the spec
  names.** A version we refuse cannot be assumed. This is a deliberate deviation
  from a SHOULD and the safe direction — the floor emits strictly fewer optional
  fields, so a header-less client is under-served rather than sent fields its
  revision does not define. See `McpSession::protocolVersion()`.
- **`title`, `instructions`, `structuredContent` and `resource_link` are no
  longer gated.** All four are defined at or below the floor, so no negotiable
  revision lacks them. Only `$schema` dialect and `icons` (both 2025-11-25) are
  still gated — they are the *only* fields that distinguish the two surviving
  revisions. An earlier version of this section claimed `structuredContent` /
  `resource_link` distinguished them too; both are `>= 2025-06-18`, which is the
  floor, so they never did.

### Wire conformance: what the MCP spec requires that is easy to miss

Departures from the spec produce no symptom until a stricter client arrives, so
they accumulate silently. Six were found in one audit and fixed together;
`tests/tst_mcpserver_protocol.cpp` pins each.

- **A POST body that is a JSON array is REFUSED.** Batching is defined by the
  **2025-03-26** base protocol and by no other revision: it does not exist in
  2024-11-05, was removed in 2025-06-18, stays absent from 2025-11-25, and is
  absent again in 2026-07-28. Since 2025-03-26 is no longer served, no supported
  revision defines the shape and the dispatch for it has been deleted.
  An array is refused explicitly rather than ignored: it parses fine, so it would
  otherwise fall through and be answered "method not found", which tells the
  sender nothing true.
- **A session the server ended answers 404**, on every verb — POST, GET and
  DELETE. That is what tells a client to re-initialize, and the `GET` case is the
  one that matters most: an SSE stream opened on a dead session never carries an
  event for it, so serving it hangs the client instead.
  **Only an explicit `DELETE` is tombstoned.** The server ends sessions three
  other ways — idle expiry, the orphan reaper inside `findOrCreateSession`, and
  `MaxTotalSessions` eviction — and none of them records. Each targets a client
  that is expected to come back, which is exactly what the auto-recovery path
  exists for, and 404ing those is the "permanently broken until restart" outcome
  that path's own comment warns about. A knowing shortfall against the MUST,
  taken because none of it has been verified against a live `mcp-remote`, Claude
  Desktop or cloud connector.
  **The auto-recovery path for IDs we never issued must stay reachable** — cloud
  connectors re-initialize per request without echoing the session header, and
  the server cannot tell an ID from before a restart from one it never issued.
  `initialize` carrying a terminated ID is still accepted; 404ing the recovery
  move would strand the client.
- **`structuredContent` is not a `ResourceContents` field.** It exists on
  `CallToolResult` only. It was being emitted inside `resources/read` contents and
  version-gated as though it were a 2025-06-18 resources feature, with a test
  pinning that mistaken premise. Removed; the same JSON is already in `text`.
  The `tools/call` `structuredContent` **is** in the schema and is unchanged.
- **Error codes are per the spec's own examples**: `-32002` (+ `data.uri`) for a
  resource that is not found, `-32602` for an unregistered tool — a bad request,
  not a server fault. Registry failures that *are* server-side (wrong sync/async
  dispatch, access level) stay `-32603`. The caller picks the code from
  `McpRegistryFailure`, an out-param, **never by matching the error text** — a
  string comparison against a message is the thing that rots.
- **SSE streams prime the client for reconnection**: a `retry` interval and an
  opening event carrying an ID and **no `data` field** — both 2025-11-25
  `SHOULD`s. An ID on every subsequent event is a `MAY`, not a SHOULD; don't cite
  all three as one requirement. The opening event omits `data` deliberately: per
  the HTML SSE model a `data` field appends value+LF, so `data: ` followed by a
  blank line dispatches a real `message` event carrying the empty string — and
  every MCP client `JSON.parse`s `event.data`. `Last-Event-ID` replay is a `MAY`
  and is deliberately not implemented, which is also why the related SHOULD that
  event IDs encode their originating stream is knowingly unmet.

**One deliberate non-conformance, recorded so the next audit does not
re-litigate it:** the server binds all interfaces rather than localhost. The
spec's localhost `SHOULD` targets servers with no LAN requirement; Decenza's MCP
endpoint is served by ShotServer, whose entire purpose is LAN reachability. The
`Origin` allowlist and the capability-URL gate on the remote surface are the
mitigations, and both already exist.

### Shot Detector Outputs (`shots_get_detail`, `shots_compare`)

`shots_get_detail` and `shots_compare` return two complementary views of the in-app Shot Summary detector pipeline:

- **`summaryLines`** — the same human-readable observation list rendered by the in-app Shot Summary dialog. Each entry is `{"text": "...", "type": "good" | "caution" | "warning" | "observation" | "verdict"}`. Useful when you want to surface the dialog's own framing.
- **`detectorResults`** — structured outputs of the detectors, intended as the primary signal for external agents. Avoids parsing prose, and the field shapes are stable across detector wording changes.

Both are populated by a single call to `ShotAnalysis::analyzeShot`, so they cannot drift: the prose lines are formatted FROM the same struct that becomes `detectorResults`. A detector flip moves both fields together.

`detectorResults` shape (fields are present only when their `checked` flag / `hasData` flag is true):

```json
{
  "channeling": { "checked": true, "severity": "none" | "transient" | "sustained", "spikeTimeSec": 18.2 },
  "flowTrend":  { "checked": true, "direction": "stable" | "rising" | "falling", "deltaMlPerSec": 0.3 },
  "preinfusion": { "observed": true, "dripWeightG": 1.4, "durationSec": 8.3 },
  "grind": {
    "checked": true, "hasData": true,
    "direction": "tooFine" | "tooCoarse" | "onTarget" | "chokedPuck" | "yieldOvershoot",
    "deltaMlPerSec": -0.6, "sampleCount": 142,
    "chokedPuck": false, "yieldOvershoot": false, "verifiedClean": false,
    "yieldRatio": 0.64,                                  // finalWeightG / targetWeightG; absent when either is 0
    "coverage": "verified" | "notAnalyzable" | "skipped",  // present unless pourTruncated cascade is active
    "gates": {                                           // present whenever Arm 2 (choked-puck arm) walked the pour;
                                                         //   absent if pressure curve was empty or beverage type skipped
      "passed": false,
      "flowSamples": 12,
      "pressurizedDurationSec": 8.76,
      "meanPressurizedFlowMlPerSec": 2.43,
      "yieldRatio": 0.64,
      "minSamples": 5,
      "minPressurizedSec": 15.0,
      "minPressureBar": 4.0,
      "chokedFlowMaxMlPerSec": 0.5,
      "chokedYieldRatioMax": 0.85,
      "yieldOvershootRatioMin": 1.20
    }
  },
  "pourTruncated": false,
  "peakPressureBar": 9.1,        // present only when pourTruncated == true
  "pourStartSec": 6.2,           // phase-boundary window analyzeShot used to gate every other detector;
                                 //   stays 0.0 when no "preinfusion"/"pour" markers are present (whole-shot fallback)
  "pourEndSec": 32.8,            // defaults to shot duration when no "end" marker is present;
                                 //   both fields are 0.0 only on the insufficient-data early return (pressure.size() < 10)
  "skipFirstFrame": false,
  "verdictCategory": "minorIssuesGrindFine"
}
```

`verdictCategory` values:
- `"clean"` — no warnings or cautions; grind was verified or skipped
- `"cleanGrindNotAnalyzable"` — no warnings or cautions, but the grind detector could not analyze this profile shape (Arm 1 windows lay before pourStart and Arm 2's pressurized-duration gate didn't fire). Distinct from `"clean"` so consumers can differentiate "verified healthy" from "we silently had no data."
- `"insufficientData"` — pressure series had fewer than 10 samples; `analyzeShot` bailed without running any detectors (every `checked` flag stays false).
- `"puckTruncated"` — pour never pressurized; channeling / grind / temp signals are unreliable
- `"skipFirstFrame"` — DE1 firmware bug or first-step too short
- `"yieldOvershoot"` — gusher (yield ran > 20% over target)
- `"chokedPuck"` — pressurized but flow ~0
- `"puckIntegrityGrindFine"` / `"puckIntegrityGrindCoarse"` / `"puckIntegrity"` — channeling-class warning, with grind direction when known
- `"minorIssuesGrindFine"` / `"minorIssuesGrindCoarse"` / `"minorIssues"` — caution-only

`grind.coverage` values:
- `"verified"` — the choked-puck loop saw enough pressurized samples to evaluate. Set whenever any arm produced data, including the choked / overshoot / direction cases (the verdict already carries the diagnosis; coverage just acknowledges the detector ran).
- `"notAnalyzable"` — espresso shot, non-degenerate window, but neither arm produced data (most often a simple two-marker Preinfusion + Pour profile where Arm 1's flow-mode window lies entirely before pourStart and Arm 2's pressurized-duration gate isn't met).
- `"skipped"` — non-espresso beverage or `grind_check_skip` analysis flag.
- Field absent — the pourTruncated cascade is suppressing the grind block entirely, OR the pour window is degenerate (`pourEnd <= pourStart`).

`grind.gates` exposes the inputs and thresholds the choked-puck arm (Arm 2) compared against, so consumers can answer "why didn't this badge fire?" without reading C++. Always emitted when Arm 2 ran — even on gate-fail paths. The arm has two split sub-arms (per #966):
- **Flow sub-arm** fires when `passed && meanPressurizedFlowMlPerSec < chokedFlowMaxMlPerSec`. `passed` requires `flowSamples >= minSamples && pressurizedDurationSec >= minPressurizedSec` (the original 15s ≥ 4 bar gate).
- **Yield sub-arm** fires when `flowSamples >= minSamples && yieldRatio < chokedYieldRatioMax`. Looser gate — only requires that the puck briefly saw meaningful pressure, not that the pressurized window was sustained.

Either sub-arm sets `chokedPuck = true`. The per-sample pressure floor is `minPressureBar`. The yield-overshoot arm runs in parallel without any pressurized-window gate and fires on `yieldRatio > yieldOvershootRatioMin`.

When a detector's `checked` (or `hasData`) flag is `false`, the detector was suppressed — most often by the `pourTruncated` cascade, but also by beverage-type skips (filter / pourover) or per-profile analysis flags (e.g. `flow_trend_ok`). Treat that as "no signal for this detector," not "clean signal."

The four legacy badge booleans (`channelingDetected`, `grindIssueDetected`, `skipFirstFrameDetected`, `pourTruncatedDetected`) remain available for backwards compatibility and are computed identically — `detectorResults` is a superset. The historical fifth (`temperatureUnstable`) and its `tempStability` block were removed end-to-end (see openspec change `remove-temperature-unstable-badge`).

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
5. **Rate limiting**: Max 60 `control` + `settings` category calls/minute per session (per `RateLimitPerMinute` constant in `mcpserver.h`). Only successful calls count against the limit. Exceeded → JSON-RPC error `-32000` with message "Rate limit exceeded"
6. **Session expiry**: 30-minute inactivity timeout (per `SessionTimeoutMinutes`), cleaned during session creation
7. **SSE limits**: Max 4 concurrent MCP SSE connections
8. **Session limits**: Max 8 concurrent **stateful** sessions (per `MaxSessions`). A session is *stateful* only while it holds a live SSE stream — the transport that needs retained server state for push (`McpSession::isStateful()`); the cap is measured by `statefulSessionCount()`, not the total session count. **Ephemeral** POST-only sessions do **not** count. This matters because the cloud MCP connectors — ChatGPT (`client="openai-mcp"`) and claude.ai (`client="Anthropic/ClaudeAI"`) — re-run `initialize` on nearly every request and never hold an SSE stream open (ChatGPT opens one momentarily per exchange; claude.ai is pure POST), so before this rule a short burst from either connector, and especially both together, would fill the pool and reject every other client with `-32000 "Too many sessions"`. Counting only stateful sessions (themselves bounded by the 4-SSE cap) means such per-request re-initializing clients can never exhaust the pool. Ephemeral sessions are released by the orphan sweep (idle bound well below the 30-minute stateful timeout), which never reaps a session holding a pending machine-start confirmation. Because ephemeral sessions are no longer bounded by `MaxSessions` and `initialize` is not rate-limited, a separate absolute backstop `MaxTotalSessions` (128) bounds total retained sessions against a tight-loop `initialize` spammer: when the pool is full, `createSession()` **evicts** the least-recently-active ephemeral session (never a stateful one, never one holding a pending confirmation) rather than rejecting — so churn can never deny service to another client, and an evicted client simply re-initializes (its in-flight async responses are decoupled from the session object). A genuine persistent subscriber (LAN `mcp-remote` / Claude Desktop) holds its SSE open and keeps full stateful behavior.
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
| 6 | None | — | Hides the Discuss button entirely |
| 7 | Claude Desktop | Uses `claudeRcSessionUrl` value | Opens a persistent Claude Code Remote Control session — requires session URL from `claude remote-control` |

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

**Registration** (3 places + sizing; historical note — the old per-surface
palette/chip lists this plan referenced were unified into one catalog table):
1. `CMakeLists.txt` — add `qml/components/layout/items/DiscussItem.qml` to QML_FILES
2. `LayoutItemDelegate.qml` — add case: `case "discuss": src = "items/DiscussItem.qml"; break`
3. `settings_network.cpp` — add a `widgetCatalogTable()` entry (palette label + chip name; drives the in-app palette, library card, and web editor)
4. `LayoutCenterZone.qml` — ensure `"discuss"` is NOT in `isAutoSized()` so it receives fixed action-button sizing (same as `history`, `espresso`, etc.)

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
3. ~~**Prerequisites**: Implement `ShotHistoryStorage::loadRecentShotsByKbIdStatic()` for dial-in history queries.~~ ✅
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

#### Net tool count change (historical, at the time of this proposal)

| Change | Count |
|--------|-------|
| Current tools | 37 |
| Remove `dialing_apply_change` | -1 |
| Remove `shots_set_feedback` | -1 |
| Add `profiles_create` | +1 |
| Add `shots_update` | +1 |
| Add `shots_delete` | +1 |
| **Subtotal at proposal time** | **38** |

After this proposal landed, additional phases added scale tools, device tools, MQTT, theme, debug log, agent file, steam health, and per-profile SAW reset. Authoritative current count: query the MCP server directly (`tools/list`) — at last verification this was **51 tools** (added `devices_reset_scale_priority`).

## Phase Status

| # | Phase | Status | Notes |
|---|-------|--------|-------|
| 1 | Settings + UI | ✅ Done | MCP settings, AI tab redesign |
| 2 | Discuss Shot | ✅ Done | Discuss button + layout widget |
| 3 | Prerequisites | ✅ Done | loadRecentShotsByKbIdStatic() |
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
2. **Discuss Shot test**: Tap Discuss on PostShotReviewPage → verify clipboard contains shot summary (when MCP off) → verify correct app opens. Test all 6 app options (Claude, ChatGPT, Gemini, Grok, Custom URL, None). Test with MCP enabled → verify no clipboard copy, toast says "it has access to your shot data".
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
- `src/history/shothistorystorage.h/cpp` — add `loadRecentShotsByKbIdStatic()` async method
- `qml/pages/settings/SettingsAITab.qml` — reorganize into sections, add MCP controls + Discuss Shot subsection
- `qml/pages/PostShotReviewPage.qml` — add Discuss button next to AI Advice button in bottom bar
- `qml/pages/ShotDetailPage.qml` — add Discuss button next to AI Advice button
- `qml/components/layout/LayoutItemDelegate.qml` — add "discuss" case to switch
- `src/core/settings_network.cpp` — add "discuss" to `widgetCatalogTable()` (palette + chip name for app, library card, and web editor)
- `qml/components/layout/LayoutCenterZone.qml` — ensure "discuss" gets fixed action-button sizing
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

## Bean Base tools and fields

- `bean_search` (read): searches the community coffee database via Visualizer's open canonical autocomplete (keyless, substring matching). Returns canonical entries (UUID shared with visualizer.coffee) enriched with origin/variety/process/roast level/tasting notes.
- `shots_get_detail` / `shots_compare` emit a parsed `beanBase` object (the shot's stored snapshot) when the shot's bean was linked; omitted otherwise.
- `shots_update` accepts a `beanBase` object — a full entry replaces the shot's snapshot, `{}` clears it (fix shots recorded against the wrong bean).
- The Bean Base API key is a sensitive credential and is NOT exposed via settings_get/settings_set.
