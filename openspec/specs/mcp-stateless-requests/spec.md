# mcp-stateless-requests Specification

## Purpose

Defines how the MCP server serves modern-era clients: rate limiting of control
and settings tools, session-independent resource notifications, and confirmation
handling that keeps no per-session state. Legacy-era behaviour is unchanged.

## Requirements

### Requirement: Control Tools Are Rate-Limited In Every Era

Tools in the control and settings categories SHALL be rate-limited in the modern
era as they are in the legacy era. The limit SHALL be counted against a key that
requires no retained protocol state, and SHALL be per-caller, not global. Until
such a limit exists, control- and settings-category tools SHALL NOT be reachable
in the modern era.

#### Scenario: A modern caller exceeds the limit

- **WHEN** a modern caller invokes control-category tools beyond the permitted rate
- **THEN** further control-category calls from that caller are refused

#### Scenario: One caller does not consume another's allowance

- **WHEN** one modern caller has exhausted its allowance
- **THEN** a different modern caller may still invoke control-category tools

#### Scenario: Control tools before a limit exists

- **WHEN** a modern client invokes a control- or settings-category tool and no session-independent rate limit has been implemented
- **THEN** the call is refused rather than served without a limit

#### Scenario: Read tools are unaffected

- **WHEN** a modern client invokes a read-category tool
- **THEN** it is served, whether or not a control-tool rate limit exists

### Requirement: Modern Clients Receive Resource Notifications Without A Session

Resource-update notifications SHALL be available to modern clients through an
opt-in subscription that depends on neither a session nor a long-lived GET
stream. A client SHALL name the notification types it wants, the server SHALL
acknowledge the types it will send, and each notification SHALL carry the
identifier of the subscription that produced it. A client SHALL NOT receive a
type it did not name.

#### Scenario: A modern client subscribes

- **WHEN** a modern client opens a subscription naming a notification type and a matching change occurs
- **THEN** the client receives a notification naming the change, tagged with its subscription identifier

#### Scenario: A client receives only what it asked for

- **WHEN** a modern client opens a subscription naming one notification type and a change of a different type occurs
- **THEN** no notification is delivered for it

#### Scenario: A modern client does not subscribe

- **WHEN** a modern client never opens a subscription
- **THEN** every resource remains readable on request

#### Scenario: The legacy subscribe verbs are gone for modern callers

- **WHEN** a modern client sends a per-resource subscribe or unsubscribe request
- **THEN** it is answered as an unknown method

#### Scenario: Legacy notifications are unaffected

- **WHEN** a legacy client holds its stream open and a resource changes
- **THEN** it receives the notification exactly as it did before the modern era existed, through the same stream and the same per-resource subscribe requests

### Requirement: Legacy Subscribe Verbs Are Withheld From Modern Callers

The per-resource subscribe and unsubscribe requests of the legacy era SHALL NOT
be served to a modern caller; the subscription mechanism replaces them. They
SHALL continue to be served to legacy callers.

#### Scenario: Legacy callers keep the per-resource verbs

- **WHEN** a legacy client sends a per-resource subscribe request
- **THEN** it is served as it was before the modern era existed

### Requirement: Request-Scoped Notifications Stay On Their Response

Notifications scoped to a single request SHALL NOT be delivered on the
subscription stream. They SHALL be delivered in the response of the request that
produced them.

#### Scenario: A request-scoped notification arrives with its response

- **WHEN** a modern request produces a notification scoped to that request
- **THEN** the notification is part of that request's response and does not appear on the stream

### Requirement: Unsubscribed Modern Clients Still Read Resources

A modern client that has not subscribed SHALL still be able to read every
resource on request. Absence of notifications SHALL degrade a client to polling,
not prevent it from working.

#### Scenario: Polling replaces notifications for an unsubscribed client

- **WHEN** a modern client holds no subscription
- **THEN** it reads each resource on request and is not blocked from using the server

### Requirement: A Tool Requiring Confirmation Is Never Silently Ungated

A tool that requires confirmation in the legacy era SHALL NOT be served
without that confirmation in the modern era.

The confirmation mechanism SHALL correlate an answer to its pending request
through an identity of its own, independent of any session, and both eras
SHALL use that one mechanism. A pending confirmation SHALL be abandoned when
the connection that requested it closes, and the caller holding that connection
SHALL be answered rather than left waiting whenever it is still reachable.

#### Scenario: A confirmation-gated tool is called by a modern client

- **WHEN** a modern client invokes a tool that requires confirmation
- **THEN** the confirmation is raised on the machine, and the tool runs only once it is accepted

#### Scenario: A confirmation is never answered because the caller vanished

- **WHEN** the connection that requested a pending confirmation closes before it is answered
- **THEN** the pending confirmation is abandoned, and no later answer can cause the tool to run

#### Scenario: A legacy caller's confirmation is unaffected

- **WHEN** a legacy client invokes a confirmation-gated tool
- **THEN** the confirmation is raised, answered and routed back exactly as it was before the modern era existed
