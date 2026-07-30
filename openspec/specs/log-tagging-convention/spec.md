# log-tagging-convention Specification

## Purpose
TBD - created by archiving change replace-scale-log-with-system-log-filter. Update Purpose after archive.
## Requirements
### Requirement: Log lines carry a subsystem marker in a fixed grammar

A subsystem whose log lines need to be retrievable as a group SHALL prefix every line it logs with a marker in the form `[Subsystem]`, optionally followed by a source tag naming the specific emitter: `[Subsystem][Source]`.

The marker SHALL be the first thing on the line, so a caller can anchor on it. The subsystem marker alone SHALL be sufficient to retrieve the whole subsystem — no line may be reachable only through its source tag — so that adding a new source cannot silently shrink what a subsystem query returns.

Markers SHALL be stable. Renaming one breaks every saved query, filter and habit built on it, so a marker is treated as a published name rather than an implementation detail.

#### Scenario: A subsystem query is complete
- **WHEN** the log is filtered on a registered subsystem marker
- **THEN** every line that subsystem logged is returned, including those carrying a source tag

#### Scenario: A new source does not shrink the subsystem query
- **WHEN** a new emitter is added to a subsystem and logs with its own source tag
- **THEN** its lines are still returned by a query on the subsystem marker alone

#### Scenario: Subsystems do not collide
- **WHEN** the log is filtered on one subsystem's marker
- **THEN** no other subsystem's lines are returned

### Requirement: Severity carries audience, in three tiers

A logging call site SHALL choose its severity by who needs the line, not by how the author feels about it:

- **DEBUG** — developer detail: protocol traffic, per-poll state, parsing internals.
- **INFO** — the narrative a user may need to understand what the app is doing: lifecycle, discovery outcomes, connections, transport decisions, scheduling.
- **WARN** and above — problems: failures, timeouts, unreachable peers, rejected data.

Tier SHALL follow audience rather than authorship: a low-level driver logs INFO when its event is part of the user-facing narrative, and a high-level manager logs DEBUG when the detail only serves a developer.

A subsystem's user-facing narrative is therefore addressable as *marker + INFO or above*, with no second token required.

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

Each subsystem SHALL apply its marker inside a logging helper — a macro or a member function — that performs the stderr write and any recording emit from one call. Call sites SHALL NOT compose a marker string themselves, and SHALL NOT write the same event through two separate outputs.

A subsystem SHALL provide a helper for each tier it uses, so a call site selects a tier by choosing a helper rather than by hand-rolling a severity. Where a source cannot emit for recording — a free function, static helper or JNI shim — a stderr-only helper variant SHALL be provided rather than letting that source hand-roll its prefix.

Helper bodies SHALL NOT be copied to specialize them; a subsystem-specific helper SHALL alias the shared one.

#### Scenario: A call site cannot drift from its own event
- **WHEN** a call site logs an event
- **THEN** one helper call produces both the stderr line and any recording emit, so the two cannot describe the event differently

#### Scenario: A source with nothing to emit still carries the marker
- **WHEN** a static helper or free function in a subsystem logs
- **THEN** it uses the stderr-only helper variant and its line still carries the subsystem marker

#### Scenario: A specialized helper aliases rather than copies
- **WHEN** a subsystem needs its own helper spelling
- **THEN** it aliases the shared helper, so a fix to the shared body reaches it

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

A build-time or pre-merge check SHALL verify that device logging helpers apply a registered marker, and that log call sites in subsystems covered by the convention go through a helper rather than composing a prefix inline.

The check SHALL be enforceable without building or running the app, so it can run per pull request. A violation SHALL fail rather than warn: a helper that forgets its marker produces lines that are silently missing from the subsystem's view and from every query that names it, which review has repeatedly failed to catch.

The set of files the check covers SHALL include every file that logs on a registered
subsystem's behalf, not only the files in which that subsystem's helpers are defined. A
file that drives a subsystem's narrative from outside its directory — a reconnect ladder
in application startup, for instance — is exactly where an unmarked line is least likely
to be noticed, because the surrounding code is not about logging at all.

Where coverage is deliberately narrower than the convention, the gap SHALL be stated in
the reference documentation, so that "the rule you follow" and "the rule the gate
enforces" are never mistaken for each other.

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

A log message SHALL NOT begin with a bracketed token that the registry does not declare.
A reader cannot distinguish `[SAW]` from `[Scale]` by looking at it, so an unregistered
bracketed prefix advertises a subsystem query that returns an incomplete answer — or
none — while looking exactly like one that works.

A subsystem currently using such a prefix SHALL take one of two paths:

- **Register it.** The token joins the registry and the subsystem gains a helper
  aliasing the shared one, at which point every rule of this convention applies to it.
- **Stop being marker-shaped.** The prefix is rewritten in a form no reader would take
  for a marker, and the lines are understood to be unretrievable as a group.

Choosing the second path SHALL be a decision, not a default: a prefix exists because
someone wanted those lines findable, and the convention's answer to that want is
registration.

This closes the one hole the enforcement check was documented as leaving open — a
hand-rolled prefix inside a helper call passed every rule, because the marker rule
matched only *registered* tokens and the bare-call rule was satisfied by the helper.

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

### Requirement: Registration is available to subsystems that are not devices

The registry SHALL NOT be limited to device and radio subsystems. Any subsystem whose
lines a reader needs to retrieve as a group — including shot-time logic that runs on
device data without owning a device — SHALL be eligible to register a marker, subject to
the same obligations as any other: a description written for someone who has never read
the code, a helper aliasing the shared one, and tiers chosen by audience.

Splitting SHALL continue to follow the question a reader is asking rather than the code's
ownership: a subsystem earns its own marker when its lines answer a different diagnostic
question, not merely because it lives in a different file.

#### Scenario: A non-device subsystem registers

- **WHEN** a subsystem that owns no device registers a marker and adds an aliased helper
- **THEN** its lines are retrievable by that marker alone, and the MCP tool description
  and enforcement check reflect it with no further edit

#### Scenario: Registration does not fragment an existing subsystem

- **WHEN** a candidate's lines answer the same diagnostic question as an already-registered
  subsystem
- **THEN** it uses that subsystem's marker with its own source tag rather than registering
  a second marker

