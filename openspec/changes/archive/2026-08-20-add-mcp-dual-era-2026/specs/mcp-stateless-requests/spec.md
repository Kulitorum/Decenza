## ADDED Requirements

### Requirement: Control Tools Are Rate-Limited In Every Era

Tools in the control and settings categories SHALL be rate-limited in the
modern era as they are in the legacy era. The limit SHALL be counted against a
key that does not require retained protocol state, and SHALL be per-caller
rather than global, so that one caller cannot exhaust another's allowance.

Until such a limit exists, control- and settings-category tools SHALL NOT be
reachable in the modern era. An unlimited path to the machine's control tools
is a worse outcome than those tools being unavailable to modern clients.

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

Resource-update notifications SHALL be available to modern clients through a
subscription mechanism that does not depend on a session or on a long-lived
GET stream.

Subscription SHALL be opt-in by notification type: a client names the types it
wishes to receive, the server acknowledges which it will send, and each
notification carries the identifier of the subscription that produced it. A
client SHALL NOT receive a type it did not name.

The per-resource subscribe and unsubscribe requests of the legacy era SHALL NOT
be served to a modern caller; this mechanism replaces them. They SHALL continue
to be served to legacy callers.

Notifications scoped to a single request SHALL NOT be delivered on this stream;
they belong to the response of the request that produced them.

A modern client that has not subscribed SHALL still be able to read every
resource on request. Absence of notifications SHALL degrade a client to
polling, not prevent it from working.

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
