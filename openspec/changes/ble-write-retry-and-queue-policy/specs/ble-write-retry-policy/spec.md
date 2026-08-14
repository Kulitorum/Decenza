## Purpose

Governs what the app does when a BLE write fails: how long it keeps retrying, what happens
to work queued behind the failure, how an upstream retry is paced against that queue, and
how a link that has stopped accepting writes is recognised while it still reports itself
connected.

## ADDED Requirements

### Requirement: The per-write retry budget is bounded near the point of diminishing return

The system SHALL bound retries of a single write to a budget beyond which observed writes do
not recover. The budget SHALL be uniform: it SHALL NOT vary with the link's recent failure
history, because a write that fails on a healthy link and one that fails on a failing link
recover at the same rate up to the bound and neither recovers past it.

The bound SHALL also be checked against the total elapsed time it permits. A budget whose
worst case exceeds the interval at which periodic writes recur leaves the link continuously
occupied and unable to become idle.

#### Scenario: A write recovers within the budget

- **WHEN** a write fails and a retry within the budget succeeds
- **THEN** the write completes normally and no failure is escalated

#### Scenario: A write exceeds the budget

- **WHEN** a write's retries reach the budget without success
- **THEN** the write is abandoned

#### Scenario: A periodic write on a degraded link

- **WHEN** a periodic write fails and its retries are exhausted
- **THEN** the elapsed time from first attempt to abandonment is shorter than the period at
  which that write recurs, so the link is idle before the next one is issued

### Requirement: Superseded and terminally-failed work is drained from the queue

When a multi-write operation is superseded by a newer one, or a write is abandoned after its
retries, the system SHALL discard the pending writes belonging to the superseded or failed
work rather than issuing them into the same link. Issuing them delays recovery, and in the
supersede case they carry values the app no longer intends to send.

#### Scenario: A profile upload supersedes an in-flight one

- **WHEN** a profile upload begins while a previous upload's writes are still pending
- **THEN** the previous upload's pending writes are discarded before the new one is issued

#### Scenario: Writes queued behind an abandoned write

- **WHEN** a write is abandoned after its retries and further writes are queued behind it
- **THEN** those writes are discarded rather than attempted

#### Scenario: The discard is recorded

- **WHEN** pending writes are discarded
- **THEN** the number discarded is recorded

### Requirement: Urgent state writes survive a queue drain

A drain SHALL NOT discard an urgent write that changes machine state. Stop and sleep requests
are issued as urgent writes and are placed in the same pending queue when another write is
already in flight, so an unqualified drain would discard a stop the user or stop-at-weight
has already commanded.

#### Scenario: A stop is pending when the queue is drained

- **WHEN** an urgent state-change write is pending and a drain occurs for any reason
- **THEN** that write is still delivered

#### Scenario: Ordinary writes are discarded around it

- **WHEN** a drain occurs with both ordinary and urgent state writes pending
- **THEN** the ordinary writes are discarded and the urgent state write is not

### Requirement: A drain invalidates any cache that would elide the re-send

When pending writes are discarded, the system SHALL invalidate any record that would let a
later identical write be skipped as unchanged. Without this, a discarded write is not delayed
but lost permanently, because the next attempt to send the same value is elided as a no-op.

#### Scenario: A discarded setting is re-sent later

- **WHEN** a write carrying a machine setting is discarded by a drain, and the same setting is
  written again afterwards
- **THEN** the later write is actually issued rather than skipped as unchanged

### Requirement: An upstream retry waits for the queue to drain

When an operation composed of several writes fails and is retried, the system SHALL NOT begin
the retry while writes from the previous attempt are still pending. A retry cadence faster
than the queue's failure rate causes each attempt to be issued into a queue that has not
drained.

The system SHALL bound this wait, so a queue that never drains cannot prevent the retry
indefinitely.

#### Scenario: Retry deferred while the previous attempt is outstanding

- **WHEN** a profile upload fails and a retry is due while writes from the failed attempt are
  still pending
- **THEN** the retry does not begin until those writes have completed or been discarded

#### Scenario: Retry proceeds once the queue is clear

- **WHEN** the previous attempt's writes have all completed or been discarded
- **THEN** the retry proceeds

#### Scenario: A queue that never drains does not block the retry forever

- **WHEN** the pending writes neither complete nor are discarded within a bounded period
- **THEN** the retry proceeds anyway

### Requirement: Pending queue depth is observable

The system SHALL record when the pending write queue grows past a depth indicating the link is
not keeping up, so a backlog is diagnosable from a submitted log rather than being visible only
as the write failures it later produces.

#### Scenario: The queue backs up

- **WHEN** the pending write queue exceeds the threshold
- **THEN** the condition and the depth are recorded

### Requirement: Consecutive write failures identify a link that has stopped accepting writes

The system SHALL count abandoned writes per link consecutively, resetting the count on any
successful write and on disconnect, and SHALL recognise a link as no longer accepting writes
once the count passes a bound.

This determination SHALL NOT rest on the reported controller state or on notification flow: a
link in this condition reports itself connected and can continue delivering notifications, so
both indicators look healthy while every command is discarded.

#### Scenario: Writes fail repeatedly while the link reports connected

- **WHEN** a link's consecutive abandoned-write count passes the bound while the controller
  reports it connected and notifications are still arriving
- **THEN** the link is recognised as no longer accepting writes

#### Scenario: A successful write clears the count

- **WHEN** a write succeeds after some abandoned writes
- **THEN** the consecutive count resets and the link is not so recognised

#### Scenario: A disconnect clears the count

- **WHEN** the link disconnects
- **THEN** the consecutive count resets, so failures observed while a link was already dying
  are not carried into the next connection

### Requirement: A link that has stopped accepting writes is self-explanatory in the log

The log entry recording this condition SHALL state that the link stopped accepting writes
while still reporting itself connected, and SHALL name what can be done about it. It SHALL be
emitted at a level the connection log views display by default, since the condition is one a
user acts on. Submitted debug logs are read by users' AI assistants, so a bare failure count
that requires knowledge of this subsystem to interpret is insufficient.

#### Scenario: Diagnosing from a submitted log

- **WHEN** this condition is recorded in a log later submitted with a bug report
- **THEN** the entry conveys the condition and names the remedy

#### Scenario: The entry is visible in the connection views

- **WHEN** the condition is recorded
- **THEN** the entry is at a level those views show without the user changing a filter
