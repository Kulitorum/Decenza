# mcp-server Specification

## Purpose
TBD - created by archiving change update-mcp-protocol-2025-11-25. Update Purpose after archive.
## Requirements
### Requirement: Latest Protocol Version Support

The MCP server SHALL declare `2025-11-25` as its preferred protocol version and SHALL also accept `2025-06-18`, `2025-03-26`, and `2024-11-05` for backward compatibility. During `initialize`, the server SHALL respond with the client-requested version when it is in the supported set, otherwise SHALL respond with the preferred version.

#### Scenario: Client requests current version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-11-25"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"`

#### Scenario: Client requests prior version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server responds with `protocolVersion: "2025-03-26"` and SHALL serve subsequent requests under that version

#### Scenario: Client requests unsupported version
- **WHEN** a client sends `initialize` with `protocolVersion: "2023-01-01"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version)

### Requirement: MCP-Protocol-Version Request Header

For every HTTP request other than `initialize`, the server SHALL accept the `MCP-Protocol-Version` request header. When present, the value MUST equal the version negotiated at `initialize` for the session; mismatches SHALL be rejected with HTTP 400. When absent, the server SHALL assume `2025-03-26` per the spec's compatibility rule.

#### Scenario: Header matches negotiated version
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2025-11-25` after negotiating `2025-11-25`
- **THEN** the server processes the request normally

#### Scenario: Header mismatch
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25`
- **THEN** the server returns HTTP 400 with body indicating protocol version mismatch

#### Scenario: Header absent on legacy client
- **WHEN** a client negotiates `2025-03-26` and POSTs subsequent requests without an `MCP-Protocol-Version` header
- **THEN** the server processes the request normally, assuming `2025-03-26`

### Requirement: Origin Header Validation

The server SHALL validate the `Origin` request header on every `/mcp` HTTP request before any JSON-RPC parsing. Empty or absent `Origin` SHALL be accepted. Non-empty `Origin` SHALL be matched against an allowlist that includes loopback addresses and the host's own LAN IP addresses; non-matching origins SHALL receive HTTP 403. CORS responses SHALL echo the validated origin back via `Access-Control-Allow-Origin` rather than wildcarding.

#### Scenario: Loopback browser request
- **WHEN** a browser at `http://localhost:3000` requests `/mcp` with `Origin: http://localhost:3000`
- **THEN** the server processes the request and responds with `Access-Control-Allow-Origin: http://localhost:3000`

#### Scenario: LAN browser request
- **WHEN** a browser on the same LAN requests `/mcp` with `Origin: http://192.168.1.50:5173` and the server's listener IP is on `192.168.1.0/24`
- **THEN** the server processes the request and echoes the origin

#### Scenario: Foreign origin
- **WHEN** any client requests `/mcp` with `Origin: http://evil.example`
- **THEN** the server returns HTTP 403 without parsing the request body

#### Scenario: CLI client without Origin
- **WHEN** `mcp-remote` POSTs `/mcp` with no `Origin` header
- **THEN** the server processes the request normally

### Requirement: Structured Tool Output

Every successful `tools/call` response SHALL include a `structuredContent` field carrying the tool's result payload as a JSON object. The existing `content` array with text content blocks SHALL be retained for backward compatibility with clients negotiating `2025-03-26` or earlier.

#### Scenario: Tool returns structured payload
- **WHEN** a client calls a tool that returns a JSON payload
- **THEN** the response includes both `content[]` (with at least one text block) and `structuredContent` (the same payload as a JSON object)

#### Scenario: Legacy client receives identical text
- **WHEN** a client negotiating `2025-03-26` calls the same tool
- **THEN** the response still includes the text content block matching the pre-upgrade behavior

### Requirement: Resource Link Content Blocks

Tools that return data backed by an MCP resource SHALL emit a `resource_link` content block referencing the canonical resource URI in addition to any inline payload. List-style tools SHALL emit one `resource_link` per item.

#### Scenario: Shot detail tool emits link
- **WHEN** a client calls `shots_get_detail` for shot `abc123`
- **THEN** the response `content[]` includes a `{"type":"resource_link","uri":"decenza://shots/abc123","title":"Shot abc123",...}` block alongside the inline shot payload

#### Scenario: Profile list tool emits links
- **WHEN** a client calls `profiles_list`
- **THEN** the response `content[]` includes one `resource_link` per profile, each pointing to `decenza://profiles/{filename}`

#### Scenario: Machine state read emits link
- **WHEN** a client calls a machine-state read tool
- **THEN** the response includes a `resource_link` referencing `decenza://machine/state`

### Requirement: Title Field on Tools and Resources

Every tool record returned by `tools/list` and every resource record returned by `resources/list` SHALL include a non-empty `title` field providing a human-readable display name distinct from the programmatic `name`/`uri`.

#### Scenario: Tool listing includes titles
- **WHEN** a client calls `tools/list`
- **THEN** every tool record includes both `name` (programmatic ID) and `title` (human-readable label)

#### Scenario: Resource listing includes titles
- **WHEN** a client calls `resources/list`
- **THEN** every resource record includes both `uri` and `title`

### Requirement: Icons on Tools and Resources

Every tool record returned by `tools/list` and every resource record returned by `resources/list` SHALL include an `icons` array with at least one entry. Each icon entry SHALL contain `src` (a `data:image/svg+xml;base64,...` URI), `mimeType: "image/svg+xml"`, and a `sizes` field. Icon assignment SHALL be driven by tool category and resource kind.

#### Scenario: Tool list includes icons
- **WHEN** a client calls `tools/list`
- **THEN** every tool record includes an `icons` array with at least one SVG entry encoded as a `data:` URI

#### Scenario: Resource list includes icons
- **WHEN** a client calls `resources/list`
- **THEN** every resource record includes an `icons` array with at least one SVG entry

### Requirement: JSON Schema 2020-12 Tool Input Schemas

Every tool's `inputSchema` SHALL be a valid JSON Schema 2020-12 document and SHALL declare `"$schema": "https://json-schema.org/draft/2020-12/schema"`. Use of pre-2020-12 keywords (`definitions`, `dependencies` as a single keyword) is prohibited.

#### Scenario: Tool list schemas declare 2020-12 dialect
- **WHEN** a client calls `tools/list`
- **THEN** every `inputSchema` object includes `"$schema": "https://json-schema.org/draft/2020-12/schema"`

#### Scenario: Schemas validate under 2020-12
- **WHEN** every `inputSchema` is fed to a JSON Schema 2020-12 validator
- **THEN** all schemas pass validation

### Requirement: Set Scale Connection-Priority Mode Tool

The MCP server SHALL expose a `devices_set_scale_priority_mode` tool that sets the persistent backoff policy mode to `enforce` or `observe`. It MUST require an explicit confirmation argument before applying. The change is eventually-consistent: it MUST be queued onto the BLE-manager thread and the response MUST NOT assert the persist has completed; the HIGH-forcing effect of `observe` additionally only applies on the next scale (re)connect and does NOT tear down the current connection. The response MUST state this queued, eventually-consistent contract explicitly. An unrecognized mode value MUST be rejected without changing state.

#### Scenario: Set observe mode
- **WHEN** the tool is called with `mode: "observe"` and `confirmed: true`
- **THEN** the mode change is queued and, once applied on the BLE-manager thread, reads back as `observe`
- **AND** the response states the change was queued (not asserted-persisted) and that HIGH-forcing applies on the next scale reconnect (the current connection is not torn down)

#### Scenario: Set back to enforce
- **WHEN** the tool is called with `mode: "enforce"` and `confirmed: true`
- **THEN** the mode change is queued and, once applied, reads back as `enforce`
- **AND** the prior persisted latch (if any) is honored again on the next reconnect

#### Scenario: Missing confirmation
- **WHEN** the tool is called without `confirmed: true`
- **THEN** no state changes and the response indicates confirmation is required

#### Scenario: Invalid mode value
- **WHEN** the tool is called with a `mode` other than `enforce` or `observe`
- **THEN** the call is rejected and the persisted mode is unchanged

### Requirement: Connection Status Reports Mode And Observe Events

The `devices_connection_status` tool SHALL report the active `backoffMode` and a bounded list of recent observe events. Each observe event MUST carry an ISO-8601 timestamp with UTC offset, the trigger kind as a human-readable string, an event kind (`wouldBackoff` or `recovered`), and the relevant duration with units in the field name (`stallSec` / `gapSec`). The list MUST be bounded (most recent first) and MUST be empty when the mode has never been `observe`.

#### Scenario: Status includes mode
- **WHEN** `devices_connection_status` is called
- **THEN** the response includes `backoffMode` as `"enforce"` or `"observe"`

#### Scenario: Observe events surfaced
- **WHEN** the mode is `observe` and one or more would-back-off and/or recovery events have occurred
- **THEN** the response includes a bounded, most-recent-first list of those events
- **AND** each event has an ISO-8601-with-offset timestamp, a human-readable trigger kind, an event kind of `wouldBackoff` or `recovered`, and a unit-suffixed duration field

#### Scenario: No observe history
- **WHEN** the mode has never been `observe` this run
- **THEN** the recent-observe-events list is empty (and `backoffMode` reports the current mode)

### Requirement: Connection Status Reports Detection Epoch And Diagnostic Build Code

The `devices_connection_status` tool SHALL report the current detection epoch and, when the scale link is latched, the build code that last set the classification as a diagnostic field. The build code MUST be presented as diagnostic ("last classified by build N"), not as the gating mechanism. Field names MUST follow the project MCP data conventions (human-readable, no Unix timestamps; the existing ISO-8601-with-offset latch timestamp is unchanged).

#### Scenario: Epoch always reported
- **WHEN** `devices_connection_status` is called
- **THEN** the scale connection-priority block includes the current detection epoch

#### Scenario: Diagnostic build code on a latched link
- **WHEN** the scale link is latched (skip-HIGH)
- **THEN** the response includes the build code that last set the classification, labeled as diagnostic
- **AND** the response does not present the build code as the rehydration gate

#### Scenario: Reset tool unchanged
- **WHEN** `devices_reset_scale_priority` is called
- **THEN** it clears the latch as before (the in-session escape hatch is unchanged by epoch scoping)

### Requirement: MCP grinder reads SHALL resolve via the equipment package
MCP surfaces that report grinder identity — the `de1://dialing` resource (`mcpresources.cpp`), `dialing_get_context`, `dialing_get_grinder_calibration`, and `ai_advisor_invoke` — SHALL resolve grinder brand/model/burrs through the equipment package (the active bag's package for live snapshots; the resolved shot's `equipment_id` for shot-derived blocks). They SHALL additionally expose the package identity (`id`, display `name`, `rpmAdjustable`) and the `rpm` dial-in. All fields SHALL follow MCP data conventions (units in field names, `kind` as a string enum, ISO-8601 timestamps).

#### Scenario: Dialing resource reports the package
- **WHEN** the `de1://dialing` resource is read
- **THEN** the grinder block SHALL include the resolved brand/model/setting plus `rpm` and `rpmAdjustable`, sourced from the active bag's equipment package

### Requirement: equipment_list tool
The MCP server SHALL provide an `equipment_list` tool (modeled on `bag_list`) returning equipment packages: `id`, display `name`, grinder `brand`/`model`/`burrs`, `rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

#### Scenario: Listing packages
- **WHEN** an agent calls `equipment_list`
- **THEN** it SHALL receive the inventory of equipment packages with the fields above

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment_select` tool (modeled on `bag_select`) that sets the active bag's equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the active bag per the dual-memory rule.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment_select` with a valid package id
- **THEN** the active bag's `equipment_id` SHALL be set to that package
- **AND** the active bag's grind setting and rpm SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment_update` tool (modeled on `bag_update`) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing bags/shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment_update` changing a package's grinder model
- **THEN** the package SHALL be updated, `rpmAdjustable` re-derived, and all referencing bags/shots SHALL resolve to the new identity

### Requirement: Equipment MCP tools SHALL read and write the basket identity
The MCP `equipment_*` tools SHALL include the package's basket identity
(`brand`, `model`) in equipment reads, and SHALL accept an optional basket identity
when creating or updating a package, alongside the grinder identity. Omitting the
basket SHALL leave a package grinder-only. Basket edits SHALL flow through the same
package-identity (dedup/fork) rules as grinder edits.

#### Scenario: Equipment read includes the basket
- **WHEN** an MCP equipment read resolves a package that has a basket
- **THEN** the response SHALL include the basket brand and model

#### Scenario: Equipment write sets a basket
- **WHEN** an MCP equipment write supplies a basket identity
- **THEN** the package SHALL gain a `kind="basket"` item subject to the identity rules

### Requirement: MCP basket fields SHALL follow the data conventions
Basket fields exposed over MCP SHALL follow the project MCP conventions: dose range
named with its unit (`doseRangeG`), and `wallProfile` / `relativeFlow` / `precision`
expressed as human-readable strings/booleans rather than numeric codes. Derived
specs SHALL be omitted when unknown (custom basket) rather than zero-filled.

#### Scenario: Basket specs are LLM-legible
- **WHEN** the MCP dialing context emits the basket sub-object
- **THEN** `wallProfile` and `relativeFlow` SHALL be readable strings and the dose range SHALL be unit-suffixed (`doseRangeG`)

