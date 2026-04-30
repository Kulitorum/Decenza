## ADDED Requirements

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
