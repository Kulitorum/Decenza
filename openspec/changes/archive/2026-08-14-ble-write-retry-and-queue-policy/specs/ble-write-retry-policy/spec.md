## Purpose

Governs what the app does when a BLE write fails: how long it keeps retrying, what happens
to work belonging to a superseded operation, how an upstream retry is paced against it, and
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

### Requirement: Superseded work is discarded, and only the superseded work

When a multi-write operation is superseded by a newer one, the system SHALL discard the pending
writes belonging to the superseded operation rather than issuing them into the same link. They
carry values the app no longer intends to send, and issuing them delays the replacement.

The discard SHALL be limited to the superseded operation's own writes. Superseding SHALL NOT be
implemented by clearing the pending queue: unrelated pending work is not superseded by a new
upload, and discarding it converts a targeted correction into an unbounded one.

An operation that has terminally failed SHALL likewise discard its own remaining pending writes.
An attempt can be declared failed while its writes are still outstanding — a failure deadline
shorter than the time a write may occupy the link makes this the normal case, not an edge one —
and those writes then sit ahead of the next attempt, which is the same defect as a supersede that
leaves its predecessor queued.

The system SHALL NOT discard pending writes merely because an *unrelated* write was abandoned
after its retries. Work queued behind someone else's failure is not itself known to be failing,
and a link that has genuinely stopped accepting writes is recognised by the consecutive-failure
rule below rather than by pre-emptively emptying the queue.

#### Scenario: A profile upload supersedes an in-flight one

- **WHEN** a profile upload begins while a previous upload's writes are still pending
- **THEN** the previous upload's pending writes are discarded before the new one is issued

#### Scenario: Unrelated pending work survives a supersede

- **WHEN** a profile upload supersedes a previous one while writes unrelated to either are pending
- **THEN** those unrelated writes are still issued

#### Scenario: An operation fails with its own writes still pending

- **WHEN** a multi-write operation is declared failed while some of its writes are still pending
- **THEN** those writes are discarded before the operation is retried

#### Scenario: An unrelated write is abandoned with work queued behind it

- **WHEN** a write is abandoned after its retries and further, unrelated writes are queued behind it
- **THEN** those writes are still attempted

#### Scenario: The discard is recorded

- **WHEN** pending writes are discarded
- **THEN** the number discarded is recorded

### Requirement: A commanded stop is never discarded

An urgent write that changes machine state SHALL be delivered regardless of any discard occurring
around it. Stop and sleep requests are issued as urgent writes, and an urgent write is placed in
the pending queue when another write is already in flight, so any discard that is not qualified
by urgency could otherwise drop a stop the user or stop-at-weight has already commanded.

This is stated as an invariant to be asserted rather than a priority mechanism to be built: the
existing stop and sleep paths already clear before issuing, which leaves the urgent write to be
sent directly rather than queued. The requirement exists so that a later change cannot quietly
remove that ordering.

#### Scenario: A stop is pending when a discard occurs

- **WHEN** an urgent state-change write is pending and a discard occurs for any reason
- **THEN** that write is still delivered

#### Scenario: Ordinary writes are discarded around it

- **WHEN** a discard occurs with both ordinary and urgent state writes pending
- **THEN** the ordinary writes are discarded and the urgent state write is not

### Requirement: A discard invalidates any cache that would elide the re-send

When pending writes are discarded, the system SHALL invalidate any record that would let a
later identical write be skipped as unchanged. Without this, a discarded write is not delayed
but lost permanently, because the next attempt to send the same value is elided as a no-op.

#### Scenario: A discarded setting is re-sent later

- **WHEN** a write carrying a machine setting is discarded, and the same setting is
  written again afterwards
- **THEN** the later write is actually issued rather than skipped as unchanged

### Requirement: An upload retry never overlaps the attempt it is retrying

When an operation composed of several writes fails and is retried, a retry attempt SHALL NOT be
issued while the previous attempt is still outstanding. A retry cadence faster than the previous
attempt's failure rate causes each attempt to be issued into a queue holding the last one, so the
queue grows with every retry instead of draining.

The system SHALL enforce this by tracking whether an attempt is outstanding, and SHALL schedule
the next attempt only once the previous one has concluded. It SHALL NOT rely on a delay chosen to
be longer than an attempt is expected to take.

An attempt SHALL be treated as concluded only once its writes are no longer pending. Tracking the
attempt alone is not sufficient: an attempt declared failed on a deadline shorter than its writes'
lifetime releases the guard while those writes are still queued, and the next attempt is then
issued behind them. Discarding the failed attempt's own writes as it concludes satisfies this.

#### Scenario: A retry becomes due while the previous attempt is outstanding

- **WHEN** a profile upload attempt is still outstanding and a retry becomes due
- **THEN** no second attempt is issued

#### Scenario: The retry follows the previous attempt's conclusion

- **WHEN** an upload attempt concludes unsuccessfully
- **THEN** the next attempt is scheduled from that point

#### Scenario: A superseded operation abandons its retries

- **WHEN** the operation being retried is superseded by a newer one
- **THEN** the outstanding retry sequence is abandoned rather than continuing against the
  superseded operation

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

Where the platform can be asked for the link's actual state, that answer MAY be used to
corroborate the determination. An inconclusive answer SHALL change nothing — it is not evidence
that the link is dead, and a possibly-live link must not be acted against on the strength of a
failed query.

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
