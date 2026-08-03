## ADDED Requirements

### Requirement: JSON-RPC Batch Requests Are Accepted

The server SHALL accept a POST body containing a JSON array of JSON-RPC messages
and process each element, at every negotiated protocol version. The response
SHALL be a JSON array holding one response per element that carried an `id`,
preserving each element's `id`. A batch consisting solely of notifications SHALL
receive HTTP 202 with no body, matching the single-notification case.

An element whose handling would be deferred — a tool requiring in-app
confirmation, or an async tool or resource — SHALL receive a JSON-RPC error in
its array slot rather than a deferred response, because a deferred response is
written to the socket as a complete HTTP body and cannot be folded into the
array. That determination SHALL be made BEFORE the element is dispatched, so
that the refused element does not run: an element refused after dispatch has
already taken its effect, and the client is told otherwise.

The session serving a batch SHALL be resolved once for the whole request, before
any element is handled. Resolving per element allows an unrecognized session
header to create one session per element, and allows a request-level outcome
discovered at element N to discard the results of elements already executed.

#### Scenario: Batch of two requests

- **WHEN** a client POSTs `[{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","id":2,"method":"tools/list"}]`
- **THEN** the server responds with a JSON array of two responses carrying `id` 1 and 2

#### Scenario: Batch of only notifications

- **WHEN** a client POSTs an array containing only messages without an `id`
- **THEN** the server responds HTTP 202 with no body

#### Scenario: Batch containing a deferring tool

- **WHEN** a batch element calls a tool that requires in-app confirmation
- **THEN** that element's array slot carries a JSON-RPC error stating the call cannot be batched, the tool is not dispatched, and the other elements are answered normally

#### Scenario: Batch carrying an unrecognized session header

- **WHEN** a batch of several elements arrives with a session ID the server does not recognize
- **THEN** at most one session is created for the whole request

#### Scenario: Single message is unaffected

- **WHEN** a client POSTs a single JSON-RPC object rather than an array
- **THEN** the server responds with a single JSON-RPC object, unchanged from prior behaviour

### Requirement: A Terminated Session Is Rejected With HTTP 404

When a client ends a session with an explicit `DELETE`, the server SHALL
remember that session ID and SHALL respond HTTP 404 to any subsequent request
carrying it — on any HTTP method, not only POST — so the client learns to start
a new session.

Sessions the server ends on its own initiative — idle expiry, orphan collection,
or eviction under a pool limit — SHALL NOT be recorded. Each of those ends a
session belonging to a client that is expected to return, and rejecting it would
defeat the recovery path described below. This is a deliberate shortfall against
the specification, which does not distinguish who ended the session; it SHALL be
revisited only against evidence from live clients.

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

#### Scenario: SSE stream on a terminated session

- **WHEN** a client sends `DELETE` with `Mcp-Session-Id: X` and then issues `GET` with `Accept: text/event-stream` carrying `Mcp-Session-Id: X`
- **THEN** the server responds HTTP 404 rather than opening a stream that can never carry an event for that session

#### Scenario: Request after session expiry

- **WHEN** a session is collected by the expiry reaper and a client then POSTs a request carrying that session ID
- **THEN** the server serves the request via the existing recovery path and does not respond 404

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

On opening an SSE stream, the server SHALL immediately send a `retry` field
giving the interval a client should wait before reconnecting, and one event
carrying an event ID and NO `data` field. The `data` field SHALL be omitted: a
`data` field appends its value and a newline to the event buffer, so an event
carrying an empty `data` is dispatched to the client as a message with empty
content, which a client parsing message data as JSON cannot consume.

Every subsequent event on the stream SHALL carry an event ID unique across all
streams within the session.

The server SHALL NOT be required to replay missed events: a `Last-Event-ID`
request header MAY be ignored, and the client recovers by re-reading the
resource named in any notification it missed.

#### Scenario: Stream opens

- **WHEN** a client issues `GET` with `Accept: text/event-stream`
- **THEN** a `retry` field is present and the first event sent carries an `id` field and no `data` field

#### Scenario: Notification carries an ID

- **WHEN** the server pushes a `notifications/resources/updated` message on the stream
- **THEN** that event carries an `id` field distinct from every other event ID in the session

#### Scenario: Client reconnects with Last-Event-ID

- **WHEN** a client reconnects sending a `Last-Event-ID` header
- **THEN** the server opens a fresh stream and is not required to replay events sent after that ID
