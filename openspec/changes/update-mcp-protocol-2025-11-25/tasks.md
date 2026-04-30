# Tasks

## 1. Protocol version negotiation

- [x] 1.1 Update `supportedVersions` in `McpServer::handleInitialize` (`src/mcp/mcpserver.cpp`) to `{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"}` with the first entry as the default.
- [x] 1.2 Persist the negotiated version on `McpSession` so per-request validation can check it (added `m_protocolVersion` field + getter/setter in `src/mcp/mcpsession.h`, default `"2025-03-26"` per spec compatibility rule).
- [ ] 1.3 Update server `serverInfo.version` if applicable. — left as-is; `version` is the Decenza app version (`1.0.0`), not the protocol version.

## 2. `MCP-Protocol-Version` header validation

- [x] 2.1 In `McpServer::handleHttpRequest`, parse the `MCP-Protocol-Version` header alongside the existing `Mcp-Session-Id` parse loop.
- [x] 2.2 For non-`initialize` requests on initialized sessions: if the header is present, compare against `session->protocolVersion()`; on mismatch return HTTP 400 with body explaining the negotiated vs. header value.
- [x] 2.3 If the header is absent, sessions stay at their default `2025-03-26` per spec.
- [x] 2.4 Echo the negotiated version on outbound responses as `MCP-Protocol-Version: <ver>` (added to `sendHttpResponse` and the SSE `200 OK` headers; also added to `Access-Control-Expose-Headers` and the `OPTIONS` preflight `Access-Control-Allow-Headers`).

## 3. Origin header validation

- [x] 3.1 Added `m_allowedOrigins` (a `QSet<QString>`) on `McpServer`, populated once in the constructor from loopback + `QNetworkInterface::allAddresses()`.
- [x] 3.2 In `handleHttpRequest`, parse `Origin:` and reject foreign values with HTTP 403 before any JSON-RPC parsing.
- [x] 3.3 Empty `Origin` is accepted (CLI clients, mcp-remote, MCP Inspector CLI).
- [x] 3.4 Replaced both `Access-Control-Allow-Origin: *` sites (the `sendHttpResponse` path and the SSE `200 OK` headers) with an echo of the validated origin via a `mcpOrigin` socket property; added `Access-Control-Allow-Credentials: true` and `Vary: Origin` when echoing. `*` remains for clients that don't send `Origin`.
- [x] ~~3.5 Settings.mcp.additionalAllowedOrigins~~ — **removed from scope**. The default allowlist (loopback + LAN + empty-Origin) covers every realistic Decenza client; per the project's "prefer fewer settings, smarter defaults" rule, no user-facing knob until a concrete need surfaces.
- [x] ~~3.6 Settings UI row~~ — **removed from scope** along with 3.5.

## 4. Structured tool output

- [x] 4.1 Added `McpServer::buildToolCallResponse(const QJsonObject&)` (in `src/mcp/mcpserver.cpp`) that emits both the existing text content block AND a `structuredContent` field carrying the same payload.
- [x] 4.2 Refactored every tool result construction site to call the helper: chat-confirmation payload, sync `tools/call`, async tool response, denied confirmation, accepted-and-resolved sync confirmation. Resource-read paths (`handleResourcesRead` sync + async) also gained `structuredContent` alongside the text block.
- [ ] 4.3 Verify with MCP Inspector that `tools/call` responses contain both `content` and `structuredContent`. — manual verification required at runtime.

## 5. Resource link content blocks

- [x] 5.1 `_resourceLinks` side-channel added to the tool result schema. `buildToolCallResponse` extracts the array and renders it as `resource_link` content blocks before the text block, then strips it from `structuredContent`.
- [x] 5.2 `mcptools_shots.cpp`: `shots_list` emits one `resource_link` per item (`decenza://shots/{id}`); `shots_get_detail` emits a single link for the requested shot.
- [x] 5.3 `mcptools_profiles.cpp`: `profiles_list` emits one link per profile (`decenza://profiles/{filename}`); `profiles_get_active` and `profiles_get_detail` each emit a single link.
- [x] 5.4 `mcptools_machine.cpp`: `machine_get_state` links to `decenza://machine/state`; `machine_get_telemetry` links to `decenza://machine/telemetry`.
- [x] 5.5 Inline payloads kept — clients that don't follow links still see full data.

## 6. `title` field on tools and resources

- [x] 6.1, 6.2 `McpToolRegistry::listTools()` now emits an auto-derived `title` field for every tool (snake_case → Title Case via `McpRegistryHelpers::deriveTitle`). No per-tool registration changes needed; explicit overrides can be added later by extending the registration API if a default reads poorly.
- [x] 6.3 `McpResourceRegistry::listResources()` now emits `title` distinct from `name`/`uri`. The existing `name` field on registrations is already a display string (e.g. "Machine State"), so it is reused for `title`; future code should treat `name` as the programmatic identifier.
- [x] 6.4, 6.5 No per-tool / per-resource churn required thanks to auto-derivation. Validated by inspection: every tool name in `mcptools_*.cpp` produces a sensible Title Case label.

## 7. Tool and resource icons

- [x] 7.1, 7.2 Both registries' list emitters now include an `icons` array.
- [x] 7.3 `McpRegistryHelpers::iconDataUri()` reads an SVG from qrc and returns `data:image/svg+xml;base64,...`. `iconsArrayFromQrc()` wraps it as `[{src, mimeType, sizes:"any"}]`.
- [x] 7.4 Tool name prefix → qrc icon mapping in `iconQrcForTool` (e.g. `machine_*` → `:/icons/decent-de1.svg`, `shots_*` → `:/icons/Graph.svg`, `profiles_*` → `:/icons/coffeebeans.svg`, `scale_*` → `:/icons/scale.svg`, `devices_*` → `:/icons/bluetooth.svg`, etc.). Substituted to existing assets in `resources/icons/` rather than creating new ones.
- [x] 7.5 Driven by the registry — no per-tool changes required.
- [x] 7.6 Resource URI prefix → qrc icon via `iconQrcForResource`; applied uniformly in `listResources`.

## 8. JSON Schema 2020-12 compliance

- [x] 8.1 Spot-check confirms no use of `definitions`/`dependencies` in tool input schemas (all use `properties`/`required`/`type`).
- [x] 8.2 `McpRegistryHelpers::withJsonSchemaDialect()` stamps `"$schema": "https://json-schema.org/draft/2020-12/schema"` on every input schema at registration time.
- [ ] 8.3 Validate each schema against a JSON Schema 2020-12 validator (e.g. `ajv` CLI on a captured `tools/list` response). — manual / CI follow-up.

## 9. Verification

- [ ] 9.1 Connect with `mcp-remote` and verify negotiation lands on `2025-11-25` and tools still work end-to-end. — runtime test.
- [ ] 9.2 Connect with MCP Inspector (web) and confirm `Origin` is echoed correctly, `tools/list` shows `title` and `icons`, and `tools/call` responses carry `structuredContent`. — runtime test.
- [ ] 9.3 Send a request with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25` → expect HTTP 400. — runtime test.
- [ ] 9.4 Send a request with `Origin: http://evil.example` → expect HTTP 403 and no JSON-RPC processing. — runtime test.
- [ ] 9.5 Confirm an old-spec client (`2025-03-26`) still works: same tool calls, text content blocks still rendered. — runtime test.
- [x] 9.6 Run `openspec validate update-mcp-protocol-2025-11-25 --strict --no-interactive` — passes.
