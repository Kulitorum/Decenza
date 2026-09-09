## Purpose

Retain available origin context and message boundaries so framework diagnostics and multiline events remain interpretable through the existing persisted log and its filters.

## ADDED Requirements

### Requirement: Available diagnostic context survives persistence

The persisted log SHALL retain supplied category and source location information for warning-or-higher runtime diagnostics when available. File paths SHALL be normalized to a portable source or QML location where possible. A diagnostic lacking origin information SHALL retain its message and original severity without inventing a file, line, component, active page or stack.

Unattributed incoming messages SHALL be retrievable under a registered runtime-diagnostic marker. Applying that fallback SHALL NOT be accepted as proof that first-party source logging conforms to the subsystem convention.

#### Scenario: A QML warning includes a source location

- **WHEN** a warning arrives with a QML file and line
- **THEN** its persisted form and MCP retrieval retain that location and the warning text

#### Scenario: A framework warning has no location

- **WHEN** a framework emits a socket warning without file or line information
- **THEN** the message remains retrievable with its original severity and an explicitly unattributed or absent location

#### Scenario: A first-party call bypasses its helper

- **WHEN** an app-owned call is captured through the unattributed runtime fallback
- **THEN** it remains a source-conformance failure rather than being counted as a successfully migrated subsystem event

### Requirement: Every physical diagnostic line is independently filterable

Each persisted physical line of a runtime message SHALL carry the elapsed-time prefix, severity and registered subsystem marker, plus its emitter context where applicable. Multiline message content SHALL retain its order and subsystem membership when filtered or paginated. Content resembling a session marker SHALL remain message content and SHALL NOT introduce a session.

Rows emitted as part of a multi-event diagnostic dump SHALL identify their dump and owner, so concurrent dumps or a page beginning after the header do not make attribution ambiguous.

#### Scenario: A multiline warning is filtered by severity

- **WHEN** a warning containing multiple physical lines is retrieved with minimum severity WARN
- **THEN** every continuation is returned with its diagnostic identity and original order

#### Scenario: A descriptor dump is filtered by subsystem

- **WHEN** a subsystem query or page retrieves FD rows without their header
- **THEN** each row still identifies its owning subsystem and dump

#### Scenario: Message content resembles a session boundary

- **WHEN** a multiline diagnostic contains text resembling a session-start banner
- **THEN** session enumeration treats it as prefixed message content rather than a new session

### Requirement: Formatting preserves existing readers and logging lifecycle

The existing log viewers and MCP interfaces SHALL retrieve the same persisted diagnostic representation. Existing registered marker names and historical log readability SHALL be preserved. Formatting SHALL NOT add network activity, UI-object access from logging threads, recursive logging, duplicate persisted events, or changes to session creation and retention.

#### Scenario: A worker emits a warning

- **WHEN** a worker thread logs a diagnostic with context
- **THEN** it is persisted once and delivered through existing readers without accessing UI state

#### Scenario: An older log is retrieved

- **WHEN** a log contains historical unformatted entries alongside newly formatted entries
- **THEN** historical entries remain readable and existing session and pagination arguments still work

#### Scenario: Shutdown emits diagnostics

- **WHEN** logging occurs during teardown after the UI is destroyed
- **THEN** formatting retains the existing logger lifetime behavior and does not access destroyed UI objects
