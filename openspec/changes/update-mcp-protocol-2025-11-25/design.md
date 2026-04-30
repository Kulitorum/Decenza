# Design: Update MCP server to spec version 2025-11-25

## Context

The MCP spec is on a roughly six-month cadence. Decenza is two revisions behind. The two skipped revisions add a mix of LLM-quality features (`structuredContent`, `resource_link`, `title`) and transport-security tightening (`MCP-Protocol-Version` header, `Origin` validation, JSON Schema 2020-12 dialect). None of these requires a wire-incompatible protocol break — clients negotiating an older version SHALL continue to work.

## Decisions

### Backward compatibility via dual content blocks

Old clients (`2025-03-26`) consume the existing `content[]` text blocks; new clients (`2025-06-18`+) consume `structuredContent`. The simplest approach is to **always emit both**:

```cpp
QJsonObject result;
result["content"] = textContentBlocks(toolResult);   // existing behavior
result["structuredContent"] = toolResult;             // NEW
return result;
```

This costs a few hundred bytes per response and avoids per-version branching in every tool. The text block can stay verbatim — clients on the new spec ignore it once `structuredContent` is present. Once we eventually drop `2025-03-26` we can remove the text duplication.

Touch points: every place we currently call `QJsonDocument(toolResult).toJson(...)` and stuff the result into a text block — `src/mcp/mcpserver.cpp:514, 598, 829, 878, 896`.

### Origin allowlist policy

Allowlist (computed once at construction, no user configuration):

- `http://localhost:*`, `http://127.0.0.1:*`, `http://[::1]:*` (and HTTPS variants)
- The host's own LAN IPs from `QNetworkInterface::allAddresses()`, any port
- Empty `Origin` (non-browser clients like `mcp-remote`, MCP Inspector CLI, curl) — accepted

Anything else: HTTP 403 before any JSON-RPC parsing. We deliberately do NOT add a user-configurable additional-origins setting — every realistic Decenza client (Claude/ChatGPT desktop via `mcp-remote`, MCP Inspector locally or via CLI, browser-based MCP frontends on the same LAN) is already covered, and per the project's "prefer fewer settings, smarter defaults" rule we don't ship a setting without a concrete user need.

Why echo-back instead of `*`: with `*`, browsers refuse to send credentials (cookies, `Authorization` headers). Once `add-remote-mcp-connector` lands and adds Bearer auth, `*` would break browser clients. Switching now is cheap and avoids a second migration later.

### `MCP-Protocol-Version` header enforcement

The spec requires the negotiated version on every HTTP request after `initialize`. Existing clients that don't send it should still work — the spec says servers MAY assume `2025-03-26` if absent, for clients that pre-date the requirement. Behavior:

- `initialize` request: header optional, version comes from JSON-RPC body.
- All subsequent requests: if header present, MUST match the negotiated version → mismatch returns HTTP 400. If absent, assume `2025-03-26`. The 400 path catches downgrade attacks and stale clients that switched protocols mid-session.

### `title` field rollout

Tool/resource records currently have `name`, `description`, `inputSchema`. Add an optional `title` to `ToolRegistration` and `ResourceRegistration` structs. Default to the existing `name` when not provided so listings stay valid during the transition. Each `mcptools_*.cpp` file is updated in a separate task to add human-readable titles.

### Resource link content blocks

Today, tools like `shots_get_detail`, `profiles_get_detail`, `shots_list` inline full payloads. The new `resource_link` content block lets a tool reference an existing resource URI (`decenza://shots/{id}`) and let the client decide whether to fetch it. Adoption strategy:

- **Read-mostly tools**: keep returning full structured payloads for now, but **also** emit a `resource_link` content block so subscribing clients can refresh on resource change notifications.
- **List tools**: emit a `resource_link` per item alongside the inline summary.
- We do not remove inline payloads — that would break clients that don't yet implement resource linking.

### Tool icons

`2025-11-25` adds an optional `icons` field on tool, resource, resource-template, and prompt records. Each icon is `{ "src": "<uri>", "mimeType": "<type>", "sizes": "<spec>" }`. We already ship a Twemoji set (`resources/emoji/`) and a small icon set under `resources/icons/`. Strategy:

- Tools get a single category-driven icon by default (e.g. `control` → ▶ play icon, `shots` → 📊 chart, `profiles` → 📋 clipboard, `settings` → ⚙ gear).
- Use existing SVG assets from `qrc:/icons/` packaged as `data:image/svg+xml;base64,...` URIs in the icon `src` field — keeps the response self-contained without needing an HTTP-served icon endpoint.
- Resources get an icon matching their kind (machine state, profile, shot).
- This is a low-priority polish task done last; if it turns out to bloat responses unacceptably we can ship without it and add an `icons=true` query parameter on `tools/list` to opt in.

### Out of scope (deliberately)

- **Elicitation** — could replace the in-app `m_pendingConfirmation` flow, but the QML modal is tightly coupled to the existing `confirmationRequested` signal; conformance benefit doesn't outweigh migration cost yet.
- **Tasks** (experimental in `2025-11-25`) — current deferred-socket pattern in `sendAsyncToolResponse` works for our async tools.
- **OAuth / RFC 8707 / OIDC discovery** — owned by `add-remote-mcp-connector`.
- **Removing JSON-RPC batching** — already non-issue: `mcpserver.cpp:237` calls `doc.object()` and rejects arrays at parse time.

## Risks

1. **`mcp-remote` compatibility regression.** `mcp-remote` is the canonical bridge for users behind LAN-only setups; we lean on it heavily. Mitigation: keep `2025-03-26` and `2024-11-05` in the negotiated set, and verify with `mcp-remote` end-to-end before merging.
2. **Origin allowlist locks out legitimate browser clients.** Mitigation: empty-Origin requests (CLI tools, `mcp-remote`, MCP Inspector CLI, curl) are unaffected, and the default allowlist (loopback + the host's own LAN IPs from `QNetworkInterface::allAddresses()`) covers every realistic Decenza browser client. No user-configurable additional-origins setting is provided — per the project's "fewer settings, smarter defaults" rule, we add one only when a concrete need surfaces.
3. **Conflict with `add-remote-mcp-connector`.** Both changes propose `mcp-server` requirements. Whichever lands second must rebase its deltas. Mitigation: we deliberately use distinct requirement names (`Latest Protocol Version Support` vs the sibling's `Streamable HTTP Protocol Version`) so each change archives cleanly even if the other has already landed; final reconciliation happens at second archive.
