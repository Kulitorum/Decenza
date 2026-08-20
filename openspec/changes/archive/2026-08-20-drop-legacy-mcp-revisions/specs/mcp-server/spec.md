## MODIFIED Requirements

### Requirement: Latest Protocol Version Support

The MCP server SHALL declare `2025-11-25` as its preferred protocol version and
SHALL also accept `2025-06-18`. It SHALL NOT accept `2025-03-26` or
`2024-11-05`, which no observed client requests and for which the protocol's own
conformance suite has no scenarios. During `initialize`, the server SHALL respond
with the client-requested version when it is in the supported set, otherwise
SHALL respond with the preferred version.

The server SHALL NOT advertise a revision it does not serve.

#### Scenario: Client requests current version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-11-25"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"`

#### Scenario: Client requests prior version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-06-18"`
- **THEN** the server responds with `protocolVersion: "2025-06-18"` and SHALL serve subsequent requests under that version

#### Scenario: Client requests a dropped version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version), the same as for any other version it does not support

#### Scenario: Client requests unsupported version
- **WHEN** a client sends `initialize` with `protocolVersion: "2023-01-01"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version)

### Requirement: MCP-Protocol-Version Request Header

For every HTTP request other than `initialize`, the server SHALL accept the `MCP-Protocol-Version` request header. When present, the value MUST equal the version negotiated at `initialize` for the session; mismatches SHALL be rejected with HTTP 400. When absent, the server SHALL assume `2025-06-18`.

The protocol's compatibility rule names `2025-03-26` as the version to assume
when the header is absent. The server no longer serves that revision, so it
cannot be assumed; the lowest supported version is assumed instead. This is a
deliberate deviation, and it is the safe direction: the lowest supported version
emits strictly fewer optional fields than any above it, so a client that omits
the header is under-served rather than sent fields its revision does not define.

A header carrying that same `2025-03-26` value SHALL be accepted rather than
rejected, and SHALL be treated exactly as an absent header. It is the value the
protocol tells a client to use when it cannot identify the version, so it
conveys no version request and SHALL NOT be read as one. Accepting it as a
header SHALL NOT make that revision negotiable, and a client sending it SHALL
NOT receive that revision's semantics.

#### Scenario: Header matches negotiated version
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2025-11-25` after negotiating `2025-11-25`
- **THEN** the server processes the request normally

#### Scenario: Header mismatch
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25`
- **THEN** the server returns HTTP 400 with body indicating protocol version mismatch

#### Scenario: Compatibility sentinel in the header
- **WHEN** a client that negotiated `2025-11-25` POSTs with `MCP-Protocol-Version: 2025-03-26`
- **THEN** the request is served under the negotiated version, not rejected and not downgraded

#### Scenario: The sentinel does not make the revision negotiable
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server still answers with its preferred version, as for any unsupported revision

#### Scenario: Header absent on legacy client
- **WHEN** a client negotiates `2025-06-18` and POSTs subsequent requests without an `MCP-Protocol-Version` header
- **THEN** the server processes the request normally, assuming `2025-06-18`

The scenario name is retained from when the assumed version was `2025-03-26`;
only the version it names has changed.

### Requirement: Structured Tool Output

Every successful `tools/call` response SHALL include a `structuredContent` field carrying the tool's result payload as a JSON object. This SHALL NOT be conditional on the negotiated version: the field is defined at the lowest revision the server serves, so no negotiable revision lacks it.

The `content` array with a text content block SHALL also be emitted, for two independent reasons that both hold: `content` is required on a tool result at every revision, and the protocol separately states that a tool returning structured content SHOULD also return the serialized JSON in a text block for backwards compatibility. Either reason alone would leave the current behaviour under-specified.

#### Scenario: Tool returns structured payload
- **WHEN** a client calls a tool that returns a JSON payload
- **THEN** the response includes both `content[]` (with at least one text block) and `structuredContent` (the same payload as a JSON object)

#### Scenario: Legacy client receives identical text
- **WHEN** a client negotiating the lowest supported revision calls the same tool
- **THEN** the response still includes the text content block

This scenario previously named `2025-03-26`, a revision no longer served. Its
point is unchanged and was never about that revision: the text block is emitted
at every revision because `content` is required on a tool result, not as a
concession to old clients.

## REMOVED Requirements

### Requirement: JSON-RPC Batch Requests Are Accepted

**Reason**: Batching is defined by exactly one revision — `2025-03-26`, whose
base protocol requires implementations to support receiving JSON-RPC batches. It
does not exist in `2024-11-05`, was removed in `2025-06-18`, stays absent from
`2025-11-25`, and is absent again in `2026-07-28`. With `2025-03-26` no longer
served, no revision this server supports defines the shape, and no observed
client has ever sent one.

**Migration**: A POST body containing a JSON array is answered with an explicit
JSON-RPC error stating that batching is not supported, rather than being
processed or silently ignored. Clients send one message per request, which is
what every revision this server now serves requires.
