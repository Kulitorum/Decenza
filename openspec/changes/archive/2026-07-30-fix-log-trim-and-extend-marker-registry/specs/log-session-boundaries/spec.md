## ADDED Requirements

### Requirement: A session marker asserts when that session began

A `SESSION START` marker in the persisted log SHALL carry the start time of the
session whose lines follow it, and SHALL be written only at the moment that session
begins.

No process SHALL write a session marker carrying a time other than the start of the
session it introduces. In particular, maintenance of the log file — trimming, rotation,
compaction — SHALL NOT synthesize a session marker, because the only start time such a
process holds is the *current* run's, and the lines it would introduce belong to an
older one.

#### Scenario: A marker's time matches its lines

- **WHEN** any `SESSION START` marker in the persisted log is read
- **THEN** the lines following it were recorded by a session that began at the time
  the marker states

#### Scenario: Trimming introduces no session

- **WHEN** the log file exceeds its size cap and is trimmed while a session is running
- **THEN** the file gains a trim banner and no `SESSION START` marker, and the number of
  sessions the file reports does not increase

#### Scenario: Two sessions never claim the same start time

- **WHEN** the sessions in a persisted log are enumerated
- **THEN** no two of them report the same start time

### Requirement: A trimmed leading fragment is reported as having an unknown start

Trimming removes content from the front of the log, which may remove a session's own
`SESSION START` marker while leaving some of its lines. Those lines SHALL be reported as
a leading fragment whose start time is **unknown**, and SHALL NOT be attributed to any
other session's start time.

A reader SHALL be able to distinguish such a fragment from a session whose marker
survived, so that a timestamp is never inferred from a boundary that was not recorded.

#### Scenario: A headless fragment is not misdated

- **WHEN** trimming removes the first session's `SESSION START` marker but leaves some of
  its lines
- **THEN** those lines are reported as a fragment with no start time, rather than
  inheriting the start time of any session

#### Scenario: A surviving marker is still reported normally

- **WHEN** trimming removes whole sessions but leaves a later session's marker intact
- **THEN** that session is reported with the start time its marker carries

### Requirement: Sessions are addressable by index in recorded order

Session enumeration SHALL list sessions in the order they were recorded, so that index
`0` is the oldest surviving session or fragment and index `-1` is the session currently
running.

A caller addressing `session=-1` SHALL receive only lines recorded by the current run.
A caller addressing any other index SHALL receive only lines recorded by that one
session, with no lines from a neighbouring session included.

#### Scenario: The newest index is the current run

- **WHEN** a caller requests `session=-1` from a log holding several sessions
- **THEN** every line returned was recorded by the run that is currently executing

#### Scenario: Indices do not skew after a trim

- **WHEN** a log has been trimmed one or more times and its sessions are enumerated
- **THEN** the indices address the sessions in recorded order, and the count matches the
  number of session boundaries actually recorded

#### Scenario: A session's lines do not leak across a boundary

- **WHEN** a caller requests a session by index
- **THEN** no line recorded before that session's start or after its end is returned
