# ble-write-retry-policy Specification

## Purpose
Governs what the app does when a BLE write fails: how long it keeps retrying, which pending writes are discarded when an operation is superseded or fails, how a retry is paced so attempts never overlap, and how a link that has stopped accepting writes is recognised and logged while it still reports itself connected.

## Requirements

### Requirement: The per-write retry budget is bounded near the point of diminishing return
The system SHALL bound retries of a single write to a uniform budget, beyond which observed writes do not recover. The budget SHALL NOT vary with the link's recent failure history. Its worst-case total elapsed time SHALL NOT exceed the interval at which periodic writes recur, so the link never stays occupied and cannot become idle.

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

#### Scenario: Budget is the same on a healthy and a failing link

- **WHEN** a write fails on a healthy link and a write fails on a failing link
- **THEN** both are retried up to the same bound, and neither is retried past it

### Requirement: Superseded work is discarded, and only the superseded work
When a multi-write operation is superseded by a newer one, the system SHALL discard the pending writes belonging to the superseded operation rather than issuing them into the same link. The discard SHALL be limited to that operation's own writes and SHALL NOT clear the pending queue. An operation that has terminally failed SHALL likewise discard its own remaining pending writes.

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

### Requirement: Failed attempts discard their own outstanding writes
An attempt declared failed while its writes are still outstanding SHALL discard those writes as it concludes, so they do not sit ahead of the next attempt.

#### Scenario: Short failure deadline leaves writes queued

- **WHEN** an attempt is declared failed on a deadline shorter than its writes' time on the link
- **THEN** its outstanding writes are discarded as it concludes and do not delay the next attempt

### Requirement: Unrelated abandoned writes do not discard the queue
The system SHALL NOT discard pending writes merely because an unrelated write was abandoned after its retries. A link that has genuinely stopped accepting writes SHALL be recognised by the consecutive-failure rule instead of by emptying the queue.

#### Scenario: Unrelated abandoned write leaves the queue alone

- **WHEN** an unrelated write is abandoned after its retries with other work queued behind it
- **THEN** that queued work is not discarded

### Requirement: A commanded stop is never discarded
An urgent write that changes machine state SHALL be delivered regardless of any discard occurring around it, including stop and sleep requests. The existing stop and sleep paths clear the queue before issuing, so the urgent write is sent directly rather than queued. This is an invariant to assert, not a mechanism to build, and no later change SHALL remove that ordering.

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
When an operation of several writes is retried, the system SHALL NOT issue a retry attempt while the previous attempt is still outstanding. It SHALL track whether an attempt is outstanding and schedule the next attempt only once the previous one has concluded, and SHALL NOT rely on a delay chosen to exceed an attempt's expected duration.

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

### Requirement: An attempt concludes only when its writes are no longer pending
An attempt SHALL be treated as concluded only once its writes are no longer pending. Declaring an attempt failed on a deadline shorter than its writes' lifetime SHALL NOT release the guard while those writes are queued.

#### Scenario: Guard holds while writes are still queued

- **WHEN** an attempt is declared failed while its writes are still queued
- **THEN** the next attempt is not issued until those writes are no longer pending

### Requirement: Pending queue depth is observable

The system SHALL record when the pending write queue grows past a depth indicating the link is
not keeping up, so a backlog is diagnosable from a submitted log rather than being visible only
as the write failures it later produces.

#### Scenario: The queue backs up

- **WHEN** the pending write queue exceeds the threshold
- **THEN** the condition and the depth are recorded

### Requirement: Consecutive write failures identify a link that has stopped accepting writes
The system SHALL count abandoned writes per link consecutively, resetting the count on any successful write and on disconnect, and SHALL recognise a link as no longer accepting writes once the count passes a bound. This determination SHALL NOT rest on the reported controller state or on notification flow, since both look healthy in this condition.

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

### Requirement: Platform link state only corroborates
Where the platform can report the link's actual state, that answer MAY corroborate the determination. An inconclusive answer SHALL change nothing, and a failed query SHALL NOT be used to act against a possibly-live link.

#### Scenario: Inconclusive platform query

- **WHEN** the platform state query fails or returns no answer
- **THEN** the consecutive-failure count is unchanged and no action is taken on the link

### Requirement: A link that has stopped accepting writes is self-explanatory in the log
The log entry recording this condition SHALL state that the link stopped accepting writes while still reporting itself connected, and SHALL name what the system does next. It SHALL NOT instruct the user to reconnect the DE1, since the system is already recovering the link. It SHALL be emitted at a level the connection log views display by default.

#### Scenario: Diagnosing from a submitted log

- **WHEN** this condition is recorded in a log later submitted with a bug report
- **THEN** the entry conveys the condition and what the system did about it

#### Scenario: The entry is visible in the connection views

- **WHEN** the condition is recorded
- **THEN** the entry is at a level those views show without the user changing a filter

#### Scenario: The entry does not ask for a manual reconnect

- **WHEN** the condition is recorded and recovery is under way
- **THEN** the entry does not direct the user to reconnect the DE1 from the Connections page or
  over MCP

### Requirement: The log entry stands alone
The entry SHALL be interpretable from a submitted log without knowledge of this subsystem. A bare failure count that needs that knowledge to read SHALL NOT suffice.

#### Scenario: Entry is read without context

- **WHEN** a submitted log is read by an AI assistant with no knowledge of the BLE subsystem
- **THEN** the entry alone states that the link stopped accepting writes and what is being done
