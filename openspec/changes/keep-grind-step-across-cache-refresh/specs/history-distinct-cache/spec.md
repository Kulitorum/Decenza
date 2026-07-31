## ADDED Requirements

### Requirement: An invalidation SHALL NOT reduce what the cache can answer

`ShotHistoryStorage`'s distinct-value cache holds both bare-column keys and composite keys
(`bean_type:<brand>`, `eq_grinder_model:<brand>`, `eq_grinder_burrs:<brand>:<model>`,
`grinder_setting:<model>`, `grinder_rpm:<model>`). A refresh SHALL leave every key that was
resident before the invalidation answerable after it — either already refilled, or with a fetch
pending.

Clearing the whole cache and repopulating only the bare columns SHALL NOT be the behaviour: it
silently narrows the cache on every shot save, shot edit and equipment change, and makes a
composite key's availability depend on whether some consumer happens to notice and re-ask.

Re-requesting SHALL be scoped to keys that were actually resident, so the cost of an invalidation
tracks what the session has genuinely used.

#### Scenario: A composite key survives a refresh

- **GIVEN** the cache holds a composite key populated earlier in the session
- **WHEN** shot history is invalidated and the refresh completes
- **THEN** that key SHALL be resident again, or a fetch for it SHALL be pending
- **AND** a consumer reading it SHALL NOT be served an empty result as though no such history existed

#### Scenario: Unused keys are not speculatively fetched

- **GIVEN** a composite key that no consumer has asked for this session
- **WHEN** an invalidation completes
- **THEN** no fetch SHALL be issued for it

### Requirement: No path SHALL leave a key absent with nothing scheduled to fill it

Every exit from the cache's fetch machinery SHALL leave the system in one of two states: the key is
populated, or a re-fetch is pending, or consumers have been told to re-ask via `distinctCacheReady`.
A silent return that leaves a key absent, with no pending work and no notification, SHALL NOT exist.

This specifically covers the discard of a result whose key was cleared mid-flight. Discarding that
result is correct — it queried before the invalidation. Returning without re-issuing it and without
signalling is what strands the consumer: nothing re-asks, so the caller's fallback stands in for the
real value indefinitely.

A re-issue SHALL be bounded — at most one — falling back to emitting `distinctCacheReady` so that
repeated invalidations cannot drive an unbounded retry loop underneath the consumer.

#### Scenario: A discarded fetch does not strand the consumer

- **GIVEN** a single-key fetch is in flight
- **WHEN** a full refresh clears its pending marker before the result arrives
- **THEN** the stale result SHALL be discarded
- **AND** either a fresh fetch for that key SHALL be issued, or `distinctCacheReady` SHALL be emitted
- **AND** the key SHALL NOT remain absent with no pending work and no notification

#### Scenario: Suggestions do not stay empty for the life of an open dialog

- **GIVEN** a dialog showing a suggestion list backed by a composite key
- **WHEN** that key's fetch is discarded by a concurrent invalidation while the dialog is open
- **THEN** the dialog SHALL receive the values without being closed and reopened

#### Scenario: Repeated invalidations do not loop

- **GIVEN** invalidations arriving faster than fetches complete
- **WHEN** fetches are repeatedly discarded
- **THEN** re-issues SHALL be bounded rather than retried indefinitely
- **AND** consumers SHALL still be notified so they can re-ask
