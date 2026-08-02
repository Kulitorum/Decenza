## ADDED Requirements

### Requirement: JSON-RPC Batch Requests Are Accepted

The server SHALL accept a POST body containing a JSON array of JSON-RPC messages
and process each element, at every negotiated protocol version. The response
SHALL be a JSON array holding one response per element that carried an `id`,
preserving each element's `id`. A batch consisting solely of notifications SHALL
receive HTTP 202 with no body, matching the single-notification case.

An element whose handling would be deferred — a tool requiring in-app
confirmation, or an async tool — SHALL receive a JSON-RPC error in its array
slot rather than a deferred response, because a deferred response is written to
the socket as a complete HTTP body and cannot be folded into the array.

#### Scenario: Batch of two requests

- **WHEN** a client POSTs `[{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","id":2,"method":"tools/list"}]`
- **THEN** the server responds with a JSON array of two responses carrying `id` 1 and 2

#### Scenario: Batch of only notifications

- **WHEN** a client POSTs an array containing only messages without an `id`
- **THEN** the server responds HTTP 202 with no body

#### Scenario: Batch containing a deferring tool

- **WHEN** a batch element calls a tool that requires in-app confirmation
- **THEN** that element's array slot carries a JSON-RPC error stating the call cannot be batched, and the other elements are answered normally

#### Scenario: Single message is unaffected

- **WHEN** a client POSTs a single JSON-RPC object rather than an array
- **THEN** the server responds with a single JSON-RPC object, unchanged from prior behaviour

### Requirement: A Terminated Session Is Rejected With HTTP 404

When the server ends a session — on an explicit `DELETE`, or when the expiry
reaper collects it — it SHALL remember that session ID and SHALL respond HTTP
404 to any subsequent request carrying it, so the client learns to start a new
session.

A session ID the server does not recognize and has not recorded as terminated
SHALL continue to be served by the existing recovery path rather than rejected,
because the server cannot distinguish an ID it issued before a restart from one
it never issued, and per-request re-initializing clients depend on that path.

The record of terminated session IDs SHALL be bounded. When the bound is
reached, the oldest record SHALL be dropped, and the ID it described SHALL
thereafter be treated as unrecognized.

#### Scenario: Request after explicit termination

- **WHEN** a client sends `DELETE` with `Mcp-Session-Id: X` and then POSTs a request carrying `Mcp-Session-Id: X`
- **THEN** the server responds HTTP 404

#### Scenario: Request after session expiry

- **WHEN** a session is collected by the expiry reaper and a client then POSTs a request carrying that session ID
- **THEN** the server responds HTTP 404

#### Scenario: Unrecognized session ID

- **WHEN** a client POSTs a request carrying a session ID the server never issued and never terminated
- **THEN** the server serves the request via the existing recovery path and does not respond 404

#### Scenario: Initialize is always accepted

- **WHEN** a client POSTs `initialize` carrying a terminated session ID
- **THEN** the server creates a new session and responds normally

### Requirement: Resource Contents Carry Only Schema-Defined Fields

Each entry in a `resources/read` `contents[]` array SHALL carry only fields
defined by the MCP `ResourceContents` schema — `uri`, `mimeType`, `_meta`, and
`text` or `blob`. The server SHALL NOT emit `structuredContent` inside a
resource content entry: that field is defined on `CallToolResult` only, and the
serialized JSON is already carried by `text`.

#### Scenario: Resource read at the current version

- **WHEN** a client negotiating `2025-11-25` reads any resource
- **THEN** each `contents[]` entry carries `uri`, `mimeType` and `text`, and no `structuredContent`

#### Scenario: Resource payload is still fully available

- **WHEN** a client reads a resource whose payload is a JSON object
- **THEN** the `text` field contains that payload serialized as JSON

### Requirement: Resource-Not-Found Reports JSON-RPC Code -32002

A `resources/read` for a URI the server does not serve SHALL return JSON-RPC
error code `-32002`, with the requested URI in the error's `data` object. Codes
for other read failures are unchanged.

#### Scenario: Unknown resource URI

- **WHEN** a client calls `resources/read` with `uri: "decenza://nonexistent"`
- **THEN** the response carries a JSON-RPC error with `code: -32002` and `data.uri` naming the requested URI

### Requirement: Unknown Tool Reports JSON-RPC Code -32602

A `tools/call` naming a tool that is not registered SHALL return JSON-RPC error
code `-32602`. Registry failures that describe a server-side fault rather than a
bad request — a tool dispatched on the wrong path, or an access level
insufficient for the caller — SHALL continue to return `-32603`.

#### Scenario: Tool name not registered

- **WHEN** a client calls `tools/call` with `name: "no_such_tool"`
- **THEN** the response carries a JSON-RPC error with `code: -32602`

#### Scenario: Access level insufficient

- **WHEN** a client calls a tool above the configured access level
- **THEN** the response carries a JSON-RPC error with `code: -32603`, unchanged

### Requirement: SSE Streams Prime Clients For Reconnection

On opening an SSE stream, the server SHALL immediately send one event carrying
an event ID and an empty `data` field, and SHALL send a `retry` field giving the
interval a client should wait before reconnecting. Every subsequent event on the
stream SHALL carry an event ID unique across all streams within the session.

The server SHALL NOT be required to replay missed events: a `Last-Event-ID`
request header MAY be ignored, and the client recovers by re-reading the
resource named in any notification it missed.

#### Scenario: Stream opens

- **WHEN** a client issues `GET` with `Accept: text/event-stream`
- **THEN** the first event sent carries an `id` field and an empty `data` field, and a `retry` field is present

#### Scenario: Notification carries an ID

- **WHEN** the server pushes a `notifications/resources/updated` message on the stream
- **THEN** that event carries an `id` field distinct from every other event ID in the session

#### Scenario: Client reconnects with Last-Event-ID

- **WHEN** a client reconnects sending a `Last-Event-ID` header
- **THEN** the server opens a fresh stream and is not required to replay events sent after that ID
