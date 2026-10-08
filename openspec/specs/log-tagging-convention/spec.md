# log-tagging-convention Specification

## Purpose
Make each subsystem's diagnostic story retrievable through stable registered markers, shared formatting helpers and audience-based severity, with source enforcement that keeps new emitters searchable.

## Requirements

### Requirement: Log lines carry a subsystem marker in a fixed grammar
Every first-party runtime diagnostic SHALL belong to a registered subsystem and SHALL prefix each line with a marker `[Subsystem]`, optionally followed by a source tag: `[Subsystem][Source]`. After the elapsed-time and severity envelope, the marker SHALL be the first thing in the message. The subsystem marker alone SHALL retrieve the whole subsystem, so no line is reachable only through its source tag. Markers SHALL be stable published names.

#### Scenario: A subsystem query is complete
- **WHEN** the log is filtered on a registered subsystem marker
- **THEN** every line that subsystem logged is returned, including those carrying a source tag

#### Scenario: A new source does not shrink the subsystem query
- **WHEN** a new emitter is added to a subsystem and logs with its own source tag
- **THEN** its lines are still returned by a query on the subsystem marker alone

#### Scenario: Subsystems do not collide
- **WHEN** the log is filtered on one subsystem's marker
- **THEN** no other subsystem's lines are returned

#### Scenario: A formerly unformatted app event is emitted

- **WHEN** a first-party C++, QML or native runtime source emits a diagnostic
- **THEN** its persisted message has a registered owner selected by the diagnostic question, rather than only a class prefix, lowercase bracket or unlabelled text

### Requirement: Severity carries audience, in three tiers
A logging call site SHALL choose its severity by who needs the line. DEBUG is for developer detail. INFO is for the narrative a user may need: lifecycle, discovery outcomes, connections, transport decisions and scheduling. WARN and above are for problems. Tier SHALL follow audience rather than authorship. A subsystem's user-facing narrative is therefore addressable as marker plus INFO or above.

#### Scenario: A narrative is addressable by marker and severity alone
- **WHEN** a caller requests a subsystem's marker at minimum level INFO
- **THEN** the result is that subsystem's user-facing narrative, without developer detail

#### Scenario: A low-level source contributes to the narrative
- **WHEN** a driver logs an event that a user needs in order to understand a connection outcome
- **THEN** it is logged at INFO despite being emitted by a low-level source

#### Scenario: Problems are addressable without naming a subsystem
- **WHEN** a caller requests minimum level WARN with no marker filter
- **THEN** problems from every subsystem are returned

### Requirement: Markers and tiers are applied by helpers, never at call sites
Each subsystem SHALL apply its marker inside a logging helper, a macro or member function, that performs the stderr write and any recording emit from one call. Call sites SHALL NOT compose a marker string, and SHALL NOT write the same event through two separate outputs. A subsystem SHALL provide a helper for each tier it uses.

#### Scenario: A call site cannot drift from its own event
- **WHEN** a call site logs an event
- **THEN** one helper call produces both the stderr line and any recording emit, so the two cannot describe the event differently

#### Scenario: A source with nothing to emit still carries the marker
- **WHEN** a static helper or free function in a subsystem logs
- **THEN** it uses the stderr-only helper variant and its line still carries the subsystem marker

#### Scenario: A specialized helper aliases rather than copies
- **WHEN** a subsystem needs its own helper spelling
- **THEN** it aliases the shared helper, so a fix to the shared body reaches it

### Requirement: Sources that cannot record use a stderr-only helper
Where a source cannot emit for recording, such as a free function, static helper or JNI shim, a stderr-only helper variant SHALL be provided rather than letting that source hand-roll its prefix. Helper bodies SHALL NOT be copied to specialize them; a subsystem-specific helper SHALL alias the shared one.

#### Scenario: A free function logs through the stderr-only helper
- **WHEN** a JNI shim or free function in a subsystem logs a line
- **THEN** it uses the stderr-only helper variant, and the line carries the subsystem marker

### Requirement: The registered markers have a single source of truth

The set of registered subsystem markers SHALL exist in exactly one place in the codebase, carrying for each marker its token and a short description of what the subsystem covers.

Every other surface that names the markers SHALL derive from that registry rather than restating them: the MCP debug log tool description, the reference documentation, and the enforcement check. Adding or changing a marker SHALL therefore require one edit.

#### Scenario: Adding a subsystem updates every surface at once
- **WHEN** a new subsystem is registered in the registry
- **THEN** the MCP tool description and the enforcement check reflect it with no further edit

#### Scenario: No surface carries an independent copy of the list
- **WHEN** the codebase is searched for a hard-coded list of markers outside the registry
- **THEN** none is found

### Requirement: The marker contract is enforced at source level
A build-time or pre-merge check SHALL verify that runtime logging helpers apply a registered marker, and that log call sites in covered subsystems go through a helper rather than composing a prefix inline. The check SHALL be enforceable without building or running the app, so it can run per pull request. A violation SHALL fail rather than warn.

#### Scenario: A helper missing its marker fails the check

- **WHEN** a new logging helper in a covered subsystem does not apply a registered marker
- **THEN** the check fails and names the helper

#### Scenario: An inline prefix fails the check

- **WHEN** a call site in a covered subsystem writes a bracketed prefix itself instead of calling a helper
- **THEN** the check fails and names the call site

#### Scenario: The check needs no build

- **WHEN** the check runs in a pull-request gate with no compiler or Qt available
- **THEN** it completes and reports its result

#### Scenario: A subsystem's line outside its own directory is covered

- **WHEN** a file outside a subsystem's directory logs an event belonging to that
  subsystem without going through its helper
- **THEN** the check fails and names the call site

#### Scenario: The coverage gap is documented rather than implied

- **WHEN** a developer reads the reference documentation for this convention
- **THEN** it states which files the check covers and which carry subsystem lines without
  being covered

#### Scenario: A QML or header call bypasses formatting

- **WHEN** a first-party QML, header or native bridge adds a raw runtime log call outside an approved helper or justified exception
- **THEN** the build-free gate fails and identifies its source location

#### Scenario: A prefix is dynamically assembled or lowercase

- **WHEN** a first-party runtime call bypasses its helper using a lowercase or dynamically assembled bracketed prefix
- **THEN** the gate rejects the bypass rather than treating its spelling as an exemption

#### Scenario: A mixed-subsystem file is checked per message
- **WHEN** a file that mixes subsystems logs an unmarked message for one of them
- **THEN** the check fails, because the file's mixed ownership does not exempt it

### Requirement: The check covers every first-party runtime emitter
The check SHALL cover all first-party runtime emitters in C++, headers, QML and platform bridges, including mixed-subsystem files and lines a file logs about a subsystem from outside that subsystem's directory. A file's mixed ownership SHALL NOT exempt all of its unmarked messages.

#### Scenario: A startup reconnect line is covered
- **WHEN** application startup code outside a subsystem's directory logs a line belonging to that subsystem without its helper
- **THEN** the check fails and names the call site

### Requirement: Exemptions are explicit and the coverage gap is documented
Generated or vendor code, test-harness output and crash-signal-safe writes SHALL be explicitly distinguished from ordinary runtime emitters. Any exemption SHALL identify a concrete call-site constraint. The reference documentation SHALL state the remaining limitations and which files the check covers.

#### Scenario: An exemption without a call-site constraint is refused
- **WHEN** a file asks for an exemption that names no concrete call-site constraint
- **THEN** the exemption is not accepted and the file's unmarked lines are checked

### Requirement: The convention is documented as the pattern for future logging

The convention SHALL be documented as reference material covering the marker grammar, the three tiers with guidance on choosing between them, how to add a helper, how to register a new subsystem, and how to retrieve a subsystem's narrative from a log or over MCP.

The documentation SHALL be discoverable from the project's instruction file alongside the other reference documents, and SHALL be written as the blueprint new logging follows rather than as a record of this change.

#### Scenario: A new subsystem has a documented path to follow
- **WHEN** a developer adds logging to a subsystem that has no marker yet
- **THEN** the documentation tells them how to register one, which helpers to add, and how tiers are chosen

#### Scenario: Retrieval is documented for both surfaces
- **WHEN** a developer or an assistant needs a subsystem's narrative from a submitted log
- **THEN** the documentation gives both the log-search form and the MCP call

### Requirement: A marker-shaped prefix is either registered or not marker-shaped
A log message SHALL NOT begin with a bracketed token that the registry does not declare, because such a prefix looks like a working subsystem query while returning an incomplete answer. A first-party subsystem using such a prefix SHALL migrate to a registered owner and shared helper. Rewriting it as an unmarked class prefix SHALL NOT satisfy the convention.

#### Scenario: An unregistered bracketed prefix fails the check

- **WHEN** a covered file logs a message beginning with a bracketed token the registry
  does not declare
- **THEN** the check fails and names the token and the call site

#### Scenario: A registered marker applied by its helper passes

- **WHEN** a call site logs through its subsystem's helper and the helper applies the
  registered marker
- **THEN** the check passes, the marker having been applied exactly once and by the
  helper

#### Scenario: A non-marker bracket is still permitted

- **WHEN** a message contains a bracketed token that is not at the start of the message —
  a protocol byte such as `[M]`, or a mode qualifier such as `[observe]`
- **THEN** the check does not flag it, because it cannot be mistaken for a line's
  subsystem marker

### Requirement: Framework diagnostics follow the runtime-context contract
Framework and unattributed diagnostics SHALL follow the runtime-context contract. They SHALL NOT be assigned to an application subsystem by guessing from the message text.

#### Scenario: An unattributed framework message is not guessed
- **WHEN** a framework diagnostic arrives with no owner
- **THEN** it is not assigned to an application subsystem based on its wording

### Requirement: Registration is available to subsystems that are not devices
The registry SHALL NOT be limited to device and radio subsystems. Any subsystem whose lines a reader retrieves as a group, including shot-time logic running on device data, SHALL be eligible to register a marker. It SHALL meet the same obligations as any other: a description written for a reader who has never read the code, an aliased helper, and tiers chosen by audience.

#### Scenario: A non-device subsystem registers

- **WHEN** a subsystem that owns no device registers a marker and adds an aliased helper
- **THEN** its lines are retrievable by that marker alone, and the MCP tool description
  and enforcement check reflect it with no further edit

#### Scenario: Registration does not fragment an existing subsystem

- **WHEN** a candidate's lines answer the same diagnostic question as an already-registered
  subsystem
- **THEN** it uses that subsystem's marker with its own source tag rather than registering
  a second marker

### Requirement: Splitting follows the diagnostic question
A subsystem SHALL earn its own marker when its lines answer a different diagnostic question, not merely because it lives in a different file. A candidate whose lines answer the same question as a registered subsystem SHALL use that marker with its own source tag.

#### Scenario: A subsystem is split by question, not by file
- **WHEN** a candidate subsystem's lines answer a diagnostic question no registered marker answers
- **THEN** it registers its own marker

### Requirement: Diagnostic wording distinguishes observations from outcomes
A runtime event SHALL describe the state actually known at the emitting point. A command request SHALL NOT be described as a confirmed physical outcome. Expected benign status SHALL NOT be logged as a fault. Recovery from a previously reported failure SHALL be available at INFO. Routine telemetry SHALL stay at DEBUG, and existing repeat suppression SHALL be preserved.

#### Scenario: Charge enable precedes a fresh OS sample

- **WHEN** the app requests charging using an OS sample taken before that command
- **THEN** the log distinguishes the request and sampled state without claiming a measured interruption duration

#### Scenario: A background request fails

- **WHEN** an update check fails with an HTTP error
- **THEN** the failure is visible at WARN, while later recovery is visible at INFO without promoting every unchanged successful check

#### Scenario: A device reports benign status

- **WHEN** a device status is classified as benign by the existing logic
- **THEN** its message does not use a problem severity or claim an error

#### Scenario: A diagnostic range has no valid samples

- **WHEN** a diagnostic has no finite range to report
- **THEN** it reports the unavailable state and relevant reason instead of a numeric range containing sentinel infinities

### Requirement: An actionable failure is not hidden below WARN
An actionable failure SHALL NOT be hidden below WARN solely because it originates in background work.

#### Scenario: A background failure is visible at WARN
- **WHEN** a background update check fails with an HTTP error
- **THEN** the failure is logged at WARN

### Requirement: Invalid numerical diagnostics are labelled
Invalid or unavailable numerical diagnostics SHALL be labelled as such, and SHALL NOT be rendered as valid measurements or ranges.

#### Scenario: A diagnostic range with no valid samples is labelled
- **WHEN** a diagnostic has no finite range to report
- **THEN** it reports the unavailable state and the reason, not a numeric range containing sentinel infinities

### Requirement: Completion is verified against source and current runtime evidence

A completed logging migration SHALL include a census of first-party runtime call sites and an unfiltered current-build log review for the exercised workflows. Any remaining unmarked or fallback first-party event SHALL be corrected or carry a documented, justified exception. A historical whole-file census SHALL NOT be presented as evidence that the current build does or does not conform.

#### Scenario: Old prefixes survive in the retained ring buffer

- **WHEN** the retained log includes pre-migration sessions
- **THEN** validation reports current-session conformance separately from historical unformatted content

#### Scenario: A dormant failure path was not exercised

- **WHEN** a first-party logging call was absent from the runtime capture
- **THEN** it remains subject to source enforcement rather than being omitted from the migration
