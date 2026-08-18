## ADDED Requirements

### Requirement: Peripheral connect ordering is established by GATT serialization, not by a timer

The system SHALL order a scale or refractometer connect against an in-progress DE1 connect through the GATT serialization guarantee, and SHALL NOT gate it on an elapsed-time cap. A fixed cap releases the second connect on a clock rather than on the DE1's actual readiness, so a DE1 connect that is slower than the cap — which happens on a slow scan or a slow characteristic discovery, with nothing wrong — produces exactly the concurrent GATT traffic the gate exists to prevent.

Ordering SHALL cover every non-DE1 peripheral, not only scales, and SHALL cover a connect initiated at any time, not only at startup.

#### Scenario: The DE1 connect takes longer than any previously fixed cap

- **WHEN** a DE1 connect and notification subscription take substantially longer than usual
- **AND** a scale connect is pending during that time
- **THEN** the scale connect is still ordered behind the DE1's in-flight GATT operations
- **AND** the DE1's connection setup completes with every required notification stream enabled

#### Scenario: No DE1 is present

- **WHEN** a scale or refractometer connect is requested and no DE1 connect is in progress, or the DE1 connect attempt has terminated in failure
- **THEN** the connect proceeds without waiting
- **AND** it does not wait out any fixed interval before starting

#### Scenario: A refractometer connect starts while the DE1 is in use

- **WHEN** a refractometer connect is initiated during a session in which the DE1 is already connected and delivering telemetry
- **THEN** the refractometer's connect and discovery are ordered against the DE1's GATT traffic
- **AND** the DE1's telemetry continues uninterrupted

### Requirement: A slow DE1 connect does not indefinitely block other peripherals

The system SHALL ensure that a DE1 connect which never reaches a terminal outcome does not prevent other peripherals from ever connecting. Ordering SHALL be released by the DE1 connect reaching success or failure, including the failure paths the system already detects for a connect that hangs.

#### Scenario: The DE1 connect hangs and is recovered

- **WHEN** a DE1 connect hangs and is aborted by the system's existing recovery for a connect stuck in `Connecting`
- **THEN** that abort is a terminal outcome for ordering purposes
- **AND** a pending scale or refractometer connect proceeds
