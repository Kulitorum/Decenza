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

A modern client that has not subscribed SHALL still be able to read every
resource on request. Absence of notifications SHALL degrade a client to
polling, not prevent it from working.

#### Scenario: A modern client subscribes

- **WHEN** a modern client opens a subscription and a subscribed resource changes
- **THEN** the client receives a notification naming the changed resource

#### Scenario: A modern client does not subscribe

- **WHEN** a modern client never opens a subscription
- **THEN** every resource remains readable on request

#### Scenario: Legacy notifications are unaffected

- **WHEN** a legacy client holds its stream open and a resource changes
- **THEN** it receives the notification exactly as it did before the modern era existed

### Requirement: A Tool Requiring Confirmation Is Never Silently Ungated

A tool that requires confirmation in the legacy era SHALL NOT be served
without that confirmation in the modern era. Where the confirmation mechanism
depends on state the modern era does not have, the tool SHALL be refused with
an error saying so.

#### Scenario: A confirmation-gated tool is called by a modern client

- **WHEN** a modern client invokes a tool that requires confirmation, and the confirmation mechanism cannot route an answer back to a caller with no session
- **THEN** the call is refused with an error explaining that the tool is unavailable to this client, rather than executed without confirmation
