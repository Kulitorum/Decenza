## Purpose

Guarantees that every Bluetooth Low Energy GATT operation from every connected peripheral — the DE1, any scale, any refractometer — passes through one queue with at most one operation outstanding, so one device's connect or service discovery can never cause another device's operation to be rejected by the platform stack.

## ADDED Requirements

### Requirement: At most one GATT operation is outstanding across all peripherals

The system SHALL permit at most one GATT operation to be in flight at any moment, counted across every connected or connecting BLE peripheral rather than per peripheral. An operation SHALL be considered in flight from the moment it is issued to the platform until it reaches a terminal outcome.

Operations covered SHALL include service discovery, characteristic discovery, characteristic read, characteristic write, and descriptor write. (Not descriptor read: nothing in the app issues one, and listing it would describe untested capability as behaviour.) Serialization SHALL NOT be limited to writes: the observed failure was a descriptor write rejected while another peripheral was performing service discovery.

Peripheral connection SHALL NOT be a queued operation. A connect that waits its turn behind queued work would let a scale reconnect stall a DE1 write, and stop-at-weight sits on that path. Instead the system SHALL apply backpressure: a scale or refractometer connect SHALL be deferred while any GATT operation is outstanding or queued, and SHALL be released when the queue reports itself drained. This is de1app's rule (`bluetooth.tcl:2276`, "Too much backpressure, waiting with the connect"), event-driven rather than polled.

#### Scenario: A second peripheral connects while the first is enabling notifications

- **WHEN** the DE1 is writing the notification-enable descriptors for its telemetry characteristics
- **AND** a scale or refractometer connect is requested during that window
- **THEN** the second peripheral's connect is deferred until the queue drains
- **AND** every one of the DE1's notification-enable descriptor writes completes successfully

#### Scenario: No DE1 is present and a scale connect is requested

- **WHEN** no machine is connected or connecting and no GATT operation is outstanding or queued
- **THEN** the scale connect proceeds immediately, with no waiting interval of any kind

#### Scenario: Two peripherals are already connected and both have work to do

- **WHEN** the DE1 and a scale each have queued GATT operations
- **THEN** the operations are interleaved one at a time rather than issued concurrently
- **AND** neither device's operations fail because the other held the stack

#### Scenario: A refractometer connects mid-session

- **WHEN** the DE1 is connected and delivering telemetry, and a refractometer connect is initiated by the user opening a page that uses it
- **THEN** the refractometer's connect and discovery are serialized against the DE1's traffic
- **AND** the DE1's notification delivery is not interrupted by the refractometer's arrival

### Requirement: The in-flight slot is released by events, not by a clock of its own

The system SHALL release the in-flight slot on an observed terminal outcome for the operation — its success callback, its error callback, or the requesting transport's teardown. Serialization SHALL NOT introduce a timer of its own to decide that an operation has finished.

Where the platform can fail to deliver any terminal callback at all, the release SHALL be driven by the requesting transport's existing terminal machinery — the machinery that already bounds that operation and verifies real state before acting — rather than by a second bound added alongside it. Two clocks racing on one operation is the condition this requirement exists to prevent.

An operation released without success SHALL be treated as failed by its own device's error handling, never silently as succeeded.

#### Scenario: An operation fails and the platform reports it

- **WHEN** the platform delivers an error for the in-flight operation
- **THEN** the slot is released at that moment, not after any further delay
- **AND** the next queued operation, on any peripheral, is issued
- **AND** the failure is reported to the requesting device

#### Scenario: The platform delivers no terminal callback

- **WHEN** an operation is issued and the platform delivers neither success nor error
- **THEN** the slot is released by the requesting transport's existing terminal handling for that operation
- **AND** no bound owned by the queue participates in that decision

#### Scenario: A peripheral disconnects while holding the slot

- **WHEN** the peripheral holding the in-flight slot disconnects or its transport is torn down
- **THEN** the slot is released immediately
- **AND** operations belonging to the disconnected peripheral are discarded rather than retried against a dead link

### Requirement: Serialization behaviour is identical on every platform

The system SHALL apply the same serialization on every supported platform, with no platform-conditional behaviour, ordering, or bound. The framework's own GATT queue is scoped to a single controller on every backend, so the cross-device guarantee is the application's responsibility everywhere.

#### Scenario: The same connect sequence runs on two platforms

- **WHEN** the same sequence of DE1 and scale connects is performed on any two supported platforms
- **THEN** the ordering of GATT operations and the conditions under which an operation is deferred are the same on both

### Requirement: Serialization does not reorder a single device's operations

The system SHALL preserve each device's own operation ordering. Serialization decides only when an operation is issued relative to other devices' operations, never the order of operations within one device.

#### Scenario: A device's multi-step sequence is interleaved with another device

- **WHEN** one peripheral has an ordered multi-step sequence outstanding (for example a profile upload's header followed by indexed frames) and another peripheral has operations queued
- **THEN** the first peripheral's steps are still issued in their original order
- **AND** the other peripheral's operations are issued between them without displacing that order

### Requirement: Every peripheral's operations are tracked as in flight

The system SHALL track every GATT operation it issues, for every peripheral, from submission to terminal outcome. An operation issued to the platform with no record that it is outstanding cannot be ordered against anything and is the condition this capability exists to remove.

#### Scenario: A scale or refractometer operation is issued

- **WHEN** a scale or refractometer performs any GATT operation
- **THEN** that operation is recorded as outstanding until its terminal outcome
- **AND** no further GATT operation, on that peripheral or any other, is issued to the platform while it is outstanding

### Requirement: Retry policy travels with the operation, not with the queue

The system SHALL attach retry behaviour to each operation rather than applying one policy to all queued work. An operation that does not request retry SHALL NOT be retried.

A device whose link has failed MUST NOT be able to occupy the shared in-flight slot for the duration of another device's retry budget.

#### Scenario: An operation submitted without retry fails

- **WHEN** an operation that requested no retry reaches an error outcome
- **THEN** it is not reissued
- **AND** the slot is released for the next queued operation

#### Scenario: An operation submitted with a retry budget fails

- **WHEN** an operation that requested a retry budget reaches an error outcome and the budget is not exhausted
- **THEN** it is reissued according to its own budget and delay
- **AND** on exhaustion it is reported as abandoned to its device rather than retried further

#### Scenario: One device's link is dead while another has work queued

- **WHEN** a peripheral whose link has failed submits operations, and another peripheral has operations queued
- **THEN** the failed peripheral's operations do not hold the slot for longer than their own policy allows
- **AND** the other peripheral's operations are dispatched
