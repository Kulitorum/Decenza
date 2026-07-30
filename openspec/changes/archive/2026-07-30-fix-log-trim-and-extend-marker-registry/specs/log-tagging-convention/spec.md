## ADDED Requirements

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

## MODIFIED Requirements

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
