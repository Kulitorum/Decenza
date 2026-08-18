## ADDED Requirements

### Requirement: A DE1 link whose required notification subscriptions failed is not reported connected

The system SHALL NOT report the DE1 as connected when any notification stream required for normal operation failed to be enabled. `STATE_INFO` and `SHOT_SAMPLE` are required: without them the app cannot observe a shot starting, cannot chart it, and cannot stop at weight, so a link missing either is not usable for making coffee regardless of what other traffic still succeeds.

On such a failure the system SHALL tear the link down and re-enter its existing reconnect path, so subscription is retried against a freshly established connection rather than against the connection that just failed it.

Retry of a failed notification-enable SHALL be bounded by the same policy as any other GATT write on that link, and SHALL NOT be unbounded or pinned at the head of the queue past that bound. On exhaustion the link SHALL be torn down rather than retried further in place.

A failure that is a permanent fact about the connection — the characteristic absent from the discovered map, or no CCCD descriptor on it — SHALL NOT be retried at all, and SHALL fail the connection at once. Retrying five times could only delay saying so.

(This requirement previously read "SHALL NOT retry a failed notification-enable in place against the same connection", which contradicted both `design.md` and the shipped code, where a rejected CCCD write takes the ordinary write policy. Corrected rather than quietly reworded.)

#### Scenario: A notification-enable is rejected during connection setup

- **WHEN** the DE1's notification-enable for a required stream is rejected by the platform, or is not confirmed within its bound
- **THEN** the DE1 is not reported as connected
- **AND** the link is torn down and a fresh connect is attempted
- **AND** the reconnected link performs its notification subscription again from the start

#### Scenario: An optional stream fails but the required streams succeed

- **WHEN** a notification-enable fails for a stream that is not required for shot observation, and every required stream was enabled
- **THEN** the DE1 is reported as connected
- **AND** the failure of the optional stream is recorded

#### Scenario: Reconnect also fails to subscribe

- **WHEN** the fresh connect's notification subscription fails in the same way
- **THEN** the system continues through its existing reconnect ladder rather than reporting a connected DE1
- **AND** the user is not shown a machine that appears ready while its telemetry is dead

### Requirement: The outcome of DE1 notification subscription is observable to the user

The system SHALL record the outcome of DE1 notification subscription at the tier the in-app connection views display, and SHALL identify which stream each outcome refers to. A subscription failure recorded only at a tier those views filter out is not observable to the person holding the machine, nor to a support reader working from a submitted log.

Where a failure is recorded, the record SHALL name the affected characteristic so the failed stream can be identified directly rather than inferred from the order in which subscriptions were attempted.

#### Scenario: A subscription fails

- **WHEN** a DE1 notification-enable fails or times out
- **THEN** the failure appears in the in-app connection views without changing any log-level setting
- **AND** the record identifies which characteristic failed

#### Scenario: Connection setup completes

- **WHEN** the DE1 finishes its notification subscription sequence
- **THEN** the record states which streams are live, so a reader can tell a fully-subscribed link from a partially-subscribed one without reconstructing it from earlier lines
