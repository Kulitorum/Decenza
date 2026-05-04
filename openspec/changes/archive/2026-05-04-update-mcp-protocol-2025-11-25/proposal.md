# Change: Update MCP server to spec version 2025-11-25

## Why

Decenza's MCP server currently advertises `2025-03-26` and `2024-11-05` (`src/mcp/mcpserver.cpp:454`). The MCP spec has since published two revisions — `2025-06-18` and `2025-11-25` (current) — adding features that materially improve LLM client experience and tighten transport security. Adopting them keeps Decenza interoperable with up-to-date clients (Claude, ChatGPT, mcp-remote, MCP Inspector) and unblocks newer features other connectors will start to require.

This change is **scoped narrowly to protocol-spec compliance**. It deliberately excludes the OAuth/federation/reachability work tracked in `add-remote-mcp-connector`; the two changes touch the same `mcp-server` capability but are independently mergeable.

## What Changes

- **BREAKING**: Bump preferred protocol version to `2025-11-25`. Continue accepting `2025-06-18`, `2025-03-26`, and `2024-11-05` for backward compatibility with older clients (notably `mcp-remote`).
- **Streamable HTTP header validation** (`2025-06-18`): require `MCP-Protocol-Version` on every HTTP request after `initialize`; reject mismatches with HTTP 400.
- **Origin header validation** (`2025-11-25`): reject requests with an `Origin` not on the loopback + LAN allowlist using HTTP 403, mitigating DNS-rebinding attacks. Replace the blanket `Access-Control-Allow-Origin: *` on `/mcp` routes with an echo-back of the validated origin.
- **Structured tool output** (`2025-06-18`): every tool result MUST include `structuredContent` alongside the existing text content block. The text block stays for clients still on `2025-03-26`.
- **Resource link results** (`2025-06-18`): tools that return shot/profile/resource references MUST emit `resource_link` content blocks rather than inlining full payloads.
- **`title` field** (`2025-06-18`): every registered tool and resource MUST carry a human-readable `title` separate from the programmatic `name`.
- **Tool / resource icons** (`2025-11-25`): each tool and resource SHOULD carry an `icons` array. Icons are sourced from `qrc:/icons/` and embedded as `data:` URIs in the response.
- **JSON Schema 2020-12 dialect** (`2025-11-25`): tool input schemas MUST validate as JSON Schema 2020-12 and SHOULD declare `"$schema": "https://json-schema.org/draft/2020-12/schema"`.

## Impact

- **Affected specs**: `mcp-server` (new capability — no current spec exists).
- **Affected code**:
  - `src/mcp/mcpserver.cpp` / `mcpserver.h` — version negotiation, header validation, Origin allowlist, CORS response shaping
  - `src/mcp/mcptoolregistry.h` — tool record gains `title` field
  - `src/mcp/mcpresourceregistry.h` and `mcpresources.cpp` — resource record gains `title` field
  - `src/mcp/mcptools_*.cpp` (10 files) — each tool registration adds `title`; each tool result adds `structuredContent`; tools returning shot/profile data emit `resource_link` blocks
  - `src/network/shotserver.cpp` — Origin allowlist must be consulted before CORS preflight responses on `/mcp` routes
- **Backward compatibility**: clients negotiating `2025-03-26` still see text content blocks; `2025-06-18`+ clients see both text and `structuredContent` and consume the latter.
- **Interaction with `add-remote-mcp-connector`**: that change separately bumps to `2025-06-18` and adds OAuth. If it lands first, this change becomes a `MODIFIED` delta of its protocol-version requirement. If this lands first, the connector change must rebase its delta against the requirement names introduced here.
