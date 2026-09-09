## MODIFIED Requirements

### Requirement: Log lines carry a subsystem marker in a fixed grammar

Every first-party runtime diagnostic SHALL belong to a registered subsystem and SHALL prefix every line it logs with a marker in the form `[Subsystem]`, optionally followed by a source tag naming the specific emitter: `[Subsystem][Source]`.

After the logger's elapsed-time and severity envelope, the marker SHALL be the first thing in the message, so a caller can anchor on it. The subsystem marker alone SHALL be sufficient to retrieve the whole subsystem — no line may be reachable only through its source tag — so that adding a new source cannot silently shrink what a subsystem query returns.

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

#### Scenario: A formerly unformatted app event is emitted

- **WHEN** a first-party C++, QML or native runtime source emits a diagnostic
- **THEN** its persisted message has a registered owner selected by the diagnostic question, rather than only a class prefix, lowercase bracket or unlabelled text


### Requirement: The marker contract is enforced at source level

A build-time or pre-merge check SHALL verify that runtime logging helpers apply a registered marker, and that log call sites in subsystems covered by the convention go through a helper rather than composing a prefix inline.

The check SHALL be enforceable without building or running the app, so it can run per pull request. A violation SHALL fail rather than warn: a helper that forgets its marker produces lines that are silently missing from the subsystem's view and from every query that names it, which review has repeatedly failed to catch.

The set of files the check covers SHALL include all first-party runtime emitters in
C++, headers, QML and platform bridges, including mixed-subsystem files. A
file that drives a subsystem's narrative from outside its directory — a reconnect ladder
in application startup, for instance — is exactly where an unmarked line is least likely
to be noticed, because the surrounding code is not about logging at all.

Generated/vendor code, test-harness output and crash-signal-safe writes SHALL be
explicitly distinguished from ordinary runtime emitters. Any necessary exemption SHALL
identify a concrete call-site constraint. A file's mixed ownership SHALL NOT be grounds
for exempting all of its unmarked messages. The reference documentation SHALL state
remaining limitations so the rule and the gate's actual coverage are not confused.

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


### Requirement: A marker-shaped prefix is either registered or not marker-shaped

A log message SHALL NOT begin with a bracketed token that the registry does not declare.
A reader cannot distinguish `[SAW]` from `[Scale]` by looking at it, so an unregistered
bracketed prefix advertises a subsystem query that returns an incomplete answer — or
none — while looking exactly like one that works.

A first-party subsystem using such a prefix SHALL migrate to a registered owner and
shared helper. Rewriting it as an unmarked class prefix SHALL NOT satisfy the convention.
Framework and unattributed diagnostics follow the runtime-context contract rather than
being assigned to an application subsystem by guessing from the message.

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

## ADDED Requirements

### Requirement: Diagnostic wording distinguishes observations from outcomes

A runtime event SHALL describe the state actually known at the emitting point. A command request SHALL NOT be described as a confirmed physical outcome. Expected benign status SHALL NOT be logged as a fault, and an actionable failure SHALL NOT be hidden below WARN solely because it originates in background work. Recovery from a previously reported failure SHALL be available at INFO.

Invalid or unavailable numerical diagnostics SHALL be labeled accordingly rather than rendered as valid measurements or ranges. Routine telemetry SHALL stay at DEBUG and existing repeat suppression SHALL be preserved.

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

### Requirement: Completion is verified against source and current runtime evidence

A completed logging migration SHALL include a census of first-party runtime call sites and an unfiltered current-build log review for the exercised workflows. Any remaining unmarked or fallback first-party event SHALL be corrected or carry a documented, justified exception. A historical whole-file census SHALL NOT be presented as evidence that the current build does or does not conform.

#### Scenario: Old prefixes survive in the retained ring buffer

- **WHEN** the retained log includes pre-migration sessions
- **THEN** validation reports current-session conformance separately from historical unformatted content

#### Scenario: A dormant failure path was not exercised

- **WHEN** a first-party logging call was absent from the runtime capture
- **THEN** it remains subject to source enforcement rather than being omitted from the migration
