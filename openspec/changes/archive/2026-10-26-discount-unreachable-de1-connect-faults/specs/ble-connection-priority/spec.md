## ADDED Requirements

### Requirement: A failed DE1 connect attempt counts only when the DE1 was advertising

A connection-class DE1 controller error on an attempt that never reached Connected SHALL count toward the DE1-fault cluster only if a DE1 was seen advertising since the previous DE1 connect attempt ended. The same error on a link that had connected, a write-failure cascade, and a zombie link SHALL count regardless of advertising.

#### Scenario: Switched-off DE1 does not build a cluster
- **WHEN** the DE1 is switched off while the scale stays connected at HIGH, and the reconnect ladder's attempts each fail with `ConnectionError`
- **AND** no DE1 advertisement is seen between attempts
- **THEN** none of those failed attempts counts toward the fault cluster
- **AND** neither a latch (enforce) nor a would-back-off (observe) results from them

#### Scenario: Contention while the DE1 is present still counts
- **WHEN** the scale is at HIGH and DE1 connect attempts fail while the DE1 is seen advertising
- **THEN** each such failure counts toward the fault cluster as before

#### Scenario: A dropped link always counts
- **WHEN** an established DE1 link drops with a connection-class controller error
- **THEN** the fault counts toward the cluster whether or not the DE1 is advertising

## MODIFIED Requirements

### Requirement: DE1-fault cluster trigger is independent of stall confirmation

The DE1-fault-cluster trigger MUST NOT be subject to scale-feed-stall confirmation.

#### Scenario: Fault cluster fires without a scale stall

- **WHEN** DE1-link faults cluster at the existing rate and window with no scale-feed stall
- **THEN** the cluster trigger fires
