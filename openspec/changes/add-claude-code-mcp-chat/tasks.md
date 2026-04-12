## 0. Approval Gate
- [ ] 0.1 Review and approve this change proposal before implementation
- [ ] 0.2 Create separate GitHub repository for the bridge project
- [ ] 0.3 Define bridge REST/SSE contract as the first deliverable of the bridge project (must match design.md in this spec)

## 1. Settings and Mode Model
- [ ] 1.1 Add assistant mode setting with values: `dialing_assistant_api` and `bridge_chat` (provider-agnostic)
- [ ] 1.2 Add bridge settings (`bridgeUrl`, `bridgeToken`, `bridgeEnabled`, connection status) — no provider-specific fields in Decenza
- [ ] 1.3 Keep existing API-key settings and `AIManager` behavior unchanged
- [ ] 1.4 Add migration/default behavior so existing users remain on API assistant mode

## 2. Bridge Client Transport
- [ ] 2.1 Implement bridge health check client (`GET /v1/health`)
- [ ] 2.2 Implement session create/send/delete methods
- [ ] 2.3 Implement session list fetch (`GET /v1/sessions`) and transcript fetch (`GET /v1/sessions/{id}/messages`)
- [ ] 2.4 Implement session rename (`PATCH /v1/sessions/{id}`)
- [ ] 2.5 Implement streaming events transport (SSE or WebSocket) with reconnect handling
- [ ] 2.6 Add structured error mapping for auth, connectivity, and bridge runtime errors

## 3. Claude Session Orchestration
- [ ] 3.1 Add a new session manager class for Claude mode (separate from `AIManager` providers)
- [ ] 3.2 Support conversation lifecycle: create named session, send message, stream output, switch session, delete session
- [ ] 3.3 Implement session list model: fetch from bridge, display name/date/message count, cache for offline viewing
- [ ] 3.4 Implement session resume: re-attach to an existing session by ID and fetch transcript on open
- [ ] 3.5 Implement context bootstrap: supply active profile summary, recent shot summary, and MCP capability snapshot at session start
- [ ] 3.6 Surface tool activity events in the conversation timeline

## 4. MCP Integration and Safety
- [ ] 4.1 Ensure Claude mode uses Decenza MCP server as tool/resource source
- [ ] 4.2 Reuse existing MCP access level restrictions and confirmation flows without bypasses
- [ ] 4.3 Include MCP capability/access summary in session bootstrap context
- [ ] 4.4 Verify rate-limiting and confirmation behavior under Claude mode traffic

## 5. QML UX
- [ ] 5.1 Add Claude mode section to `AISettingsPage.qml` (bridge URL/token, connect test, status)
- [ ] 5.2 Add session list page/view: session name, date, message count, resume/rename/delete actions
- [ ] 5.3 Add dedicated chat page/view for Claude mode with streaming text and tool activity indicators
- [ ] 5.4 Add new session flow: prompt for optional name before first message
- [ ] 5.5 Add explicit mode switch UI and explain that API assistant remains available
- [ ] 5.6 Add resilient offline/reconnect states with retry action

## 6. Telemetry, Logging, and Privacy
- [ ] 6.1 Add debug logging for bridge session lifecycle (without leaking secrets)
- [ ] 6.2 Add user-facing controls to clear Claude mode conversation history
- [ ] 6.3 Ensure bridge tokens are handled as sensitive settings in UI/logging

## 7. Build and Wiring
- [ ] 7.1 Add new source/QML files to `CMakeLists.txt`
- [ ] 7.2 Wire new components in startup/main controller construction
- [ ] 7.3 Verify desktop, Android, and iOS compile/runtime wiring

## 8. Bridge Project (separate repo — tracked here for reference only)
- [ ] 8.1 [bridge] Docker image with Claude Code SDK and bridge service
- [ ] 8.2 [bridge] docker-compose.yml with volume mount for Claude auth credentials
- [ ] 8.3 [bridge] One-time `claude auth login` flow documented in bridge README
- [ ] 8.4 [bridge] Durable session transcript storage (survives container restart)
- [ ] 8.5 [bridge] REST/SSE endpoints matching contract in design.md
- [ ] 8.6 [bridge] Pairing token generation and validation
- [ ] **Phase 2** [bridge] Native Mac menu bar app (removes Docker Desktop dependency)
- [ ] **Phase 2** [bridge] Windows service installer

## 9. Validation
- [ ] 9.1 Manual test: existing API-key Dialing Assistant unchanged
- [ ] 9.2 Manual test: bridge chat can connect, chat, and stream responses via Docker bridge
- [ ] 9.3 Manual test: bridge chat can read MCP context and perform safe tool calls
- [ ] 9.4 Manual test: dangerous MCP actions still trigger in-app confirmation
- [ ] 9.5 Manual test: bridge disconnect/reconnect and session resume behavior
- [ ] 9.6 Manual test: session created on day 1 is resumable on day 3+ with full transcript intact
- [ ] 9.7 Manual test: session rename propagates to session list immediately
- [ ] 9.8 Manual test: session delete removes session and transcript; cannot be recovered
