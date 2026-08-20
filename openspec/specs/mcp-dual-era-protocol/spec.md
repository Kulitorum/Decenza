# mcp-dual-era-protocol Specification

## Purpose
TBD - created by archiving change add-mcp-dual-era-2026. Update Purpose after archive.

## Requirements

### Requirement: One Endpoint Serves Both Protocol Eras

The server SHALL serve both protocol eras on the same endpoint: the legacy
era, in which a client establishes a session with an `initialize` handshake,
and the modern era, in which every request carries its own protocol version as
per-request metadata.

The era SHALL be selected per request, from the request itself. The server
SHALL NOT require configuration, a separate port, or a build option to choose
between them.

#### Scenario: A legacy client connects

- **WHEN** a client sends an `initialize` request
- **THEN** the server serves that client under the negotiated legacy revision, unchanged from its behaviour before the modern era existed

#### Scenario: A modern client connects

- **WHEN** a client sends a request carrying modern per-request protocol metadata
- **THEN** the server serves that request under the modern revision, without establishing a session

#### Scenario: Both eras are served at once

- **WHEN** a legacy client and a modern client are both talking to the server
- **THEN** each is served under its own era, and neither affects the other

### Requirement: Legacy Behaviour Is Unchanged

Adding the modern era SHALL NOT change any observable behaviour of any legacy
revision the server negotiates. A client that works today SHALL continue to
work with no change on its side.

#### Scenario: An existing client is unaffected

- **WHEN** a client that negotiated a legacy revision before the modern era existed makes any request it made before
- **THEN** it receives the same response it received before

### Requirement: An Ambiguous Request Is Served As Legacy

When the server cannot determine which era a request belongs to, it SHALL
serve it as legacy.

This is not a neutral default. Serving a legacy request as modern breaks a
client that works today and gives it no way to recover. Serving a modern
request as legacy produces an error the modern client's own detection is
specified to recognize and fall back from.

#### Scenario: Era cannot be determined

- **WHEN** a request carries neither an `initialize` method nor modern per-request protocol metadata
- **THEN** the server serves it under the legacy era

### Requirement: The Server Advertises Its Supported Versions

The server SHALL implement a discovery request that reports the protocol
versions it supports, its identity, and its capabilities, so that a modern
client MAY learn them before sending any other request.

A client SHALL NOT be required to call it: a modern request naming a version
the server does not support SHALL be answered with an unsupported-version
error carrying the list of versions the server does support, so that a client
that invokes a method directly can retry with a mutually supported version.

#### Scenario: Client discovers up front

- **WHEN** a modern client sends the discovery request
- **THEN** the response names every protocol version the server supports

#### Scenario: Client invokes a method directly with an unsupported version

- **WHEN** a modern client sends a request naming a protocol version the server does not support
- **THEN** the response is an unsupported-version error listing the versions the server does support

#### Scenario: Advertised versions are the versions served

- **WHEN** the discovery request reports a protocol version
- **THEN** a request naming that version is served rather than rejected as unsupported

### Requirement: A Modern Request Carries Its Own Protocol Version

In the modern era the protocol version SHALL be taken from the request, and
SHALL NOT be read from retained session state. Where the version appears both
in the request body and in a transport header, the two SHALL agree, and a
request whose header and body disagree SHALL be rejected.

#### Scenario: Version comes from the request

- **WHEN** two modern requests arrive naming different supported protocol versions
- **THEN** each is answered according to the version it named

#### Scenario: Header and body disagree

- **WHEN** a modern request declares one protocol version in its transport header and a different one in its body
- **THEN** the request is rejected rather than served under either

### Requirement: A Method Removed By The Modern Era Is Not Served To A Modern Caller

Methods the modern revision removes — the liveness check, the log-level setter,
and the roots-list-changed notification — SHALL NOT be served to a modern
caller, and SHALL continue to be served to a legacy caller exactly as before.

#### Scenario: A modern caller invokes a removed method

- **WHEN** a modern request names a method the modern revision removed
- **THEN** it is answered as an unknown method

#### Scenario: A legacy caller invokes the same method

- **WHEN** a legacy client invokes that method
- **THEN** it is served exactly as it was before the modern era existed

### Requirement: Every Modern Result States Its Type And Its Origin

Every result served to a modern caller SHALL declare its result type, and SHALL
carry the server's own identity in its metadata.

The server SHALL always declare the type meaning a complete result. It SHALL
NOT declare the type reserved for interim results awaiting further client
input, because it never returns one.

The identity SHALL be the same identity the legacy handshake reports, since a
modern caller performs no handshake and has no other occasion to learn it.

#### Scenario: An ordinary modern result

- **WHEN** a modern client receives any result
- **THEN** that result declares itself complete, and names the server

#### Scenario: Legacy results are unchanged

- **WHEN** a legacy client receives any result
- **THEN** it carries neither field, exactly as before

### Requirement: Both Eras Are Served On Every Route That Reaches The Server

Every caller that dispatches HTTP requests into the MCP server SHALL serve
both eras. Era support SHALL NOT differ between the local route and the remote
route.

#### Scenario: Modern request over the remote route

- **WHEN** a modern request arrives over the tokenized remote route rather than the local one
- **THEN** it is served under the modern era, the same as it would be locally
