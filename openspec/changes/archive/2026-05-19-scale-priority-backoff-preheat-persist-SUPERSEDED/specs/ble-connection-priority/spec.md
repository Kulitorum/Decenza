## MODIFIED Requirements

### Requirement: Scale Weight-Feed Liveness Is The In-Shot Backstop

The system SHALL additionally treat a connected scale failing to deliver weight notifications at its expected cadence **whenever weight is expected to stream** as a dual-HIGH-incapable trigger, even if no DE1 error has been observed. "Weight is expected to stream" SHALL cover two gated conditions, not extraction alone: (a) the pre-shot **EspressoPreheating** phase (the espresso cycle has started, the scale is connected, tare is complete) so the stall can be caught during warm-up and the backoff + reconnect can begin before the pour; and (b) **active extraction** (the existing in-shot backstop). Both conditions are driven by real espresso-cycle traffic, so the detector cannot fire on a legitimately idle device. The liveness evaluation SHALL remain gated so that a legitimately idle scale with no espresso cycle in progress, or the absence of a scale, is never treated as a fault. It SHALL be evaluated in the scale-agnostic extraction / weight-processing path so it applies to any connected scale type, and SHALL NOT depend on any individual scale driver's internal liveness mechanism. The skip-HIGH latch it sets is the `BLEManager` latch (persistence semantics per the App-Run Scoped requirement).

#### Scenario: Scale stalls during preheating (caught before the pour)
- **WHEN** the espresso cycle has started (machine in EspressoPreheating), the scale is connected and tare is complete, AND the scale stops delivering weight notifications beyond the reconnect-gap threshold
- **THEN** the system SHALL treat the device as dual-HIGH-incapable and proceed to back off during preheating, before extraction starts where the preheat duration allows

#### Scenario: Scale stalls during active extraction (backstop still applies)
- **WHEN** extraction is active and weight should be changing AND the connected scale stops delivering weight notifications beyond the reconnect-gap threshold AND no qualifying DE1 cluster occurred earlier in the run
- **THEN** the system SHALL treat the device as dual-HIGH-incapable and proceed to back off

#### Scenario: Idle scale (no espresso cycle) is not a fault
- **WHEN** the scale is connected but no espresso cycle is in progress (neither preheating nor extracting) and the scale is quiet
- **THEN** the system SHALL NOT treat the quiet feed as a fault and SHALL NOT trigger a backoff

#### Scenario: No scale attached
- **WHEN** no scale is connected
- **THEN** the detection and backoff logic SHALL be inert and SHALL NOT trigger any priority change

### Requirement: DE1 Link Always Requests High Priority

The system SHALL request `CONNECTION_PRIORITY_HIGH` on the DE1 BLE link after the DE1 controller connects, independent of Android version, **except** when the dual-HIGH-incapable latch is set — in which case it SHALL skip the HIGH request and let the DE1 link come up at the platform-default BALANCED interval. When latched, both the DE1 and the scale links therefore run at BALANCED (the known-good configuration for a dual-HIGH-incapable radio). The decision SHALL consult the same `BLEManager` latch the scale transport consults (it is a device-level property, not per-link). It is eventually-consistent: a latch set mid-run takes effect on the DE1's next connect (the system SHALL NOT renegotiate an already-connected DE1 link's priority). The DE1 priority decision (request HIGH, or skip because latched) SHALL be logged.

#### Scenario: DE1 connects, latch not set
- **WHEN** the DE1 controller reaches the connected state AND the dual-HIGH-incapable latch is not set
- **THEN** the system SHALL request HIGH priority on the DE1 link
- **AND** this request SHALL NOT be gated by Android SDK version

#### Scenario: DE1 connects, latch set (both links BALANCED)
- **WHEN** the DE1 controller reaches the connected state AND the dual-HIGH-incapable latch is set
- **THEN** the system SHALL NOT request HIGH on the DE1 link and it SHALL come up at the platform-default BALANCED interval
- **AND** combined with the scale also skipping HIGH, both links SHALL run at BALANCED

#### Scenario: Latch set mid-run is eventually-consistent for the DE1
- **WHEN** the latch is set while the DE1 is already connected at HIGH
- **THEN** the system SHALL NOT renegotiate the live DE1 link
- **AND** the DE1 SHALL come up at BALANCED on its next connect (app restart or DE1 reconnect)

#### Scenario: Capable hardware keeps DE1 at HIGH
- **WHEN** detection never fires (capable device, latch never set)
- **THEN** the DE1 link SHALL request and retain HIGH priority exactly as before this change (no regression)

### Requirement: App-Run Scoped, Not Persisted

The system SHALL persist the dual-HIGH-incapable classification across application restarts via the settings system, stored as an **internal** record in a settings domain sub-object (no `Q_PROPERTY`, no QML/Settings-UI binding, no `Settings`-facade property). The persisted record SHALL include: the latched flag, the trigger kind, the ISO-8601 set-time, and the application build number (`versionCode()`) that set it. It SHALL be loaded once before any BLE connect and mirrored into the in-memory `BLEManager` latch, so the first connect of a run already reflects a prior classification (no detection window on a known-weak device). It SHALL be written through when the latch is set and cleared when the latch is cleared (including via the MCP reset). The persisted classification SHALL be **build-scoped**: if the stored build number does not equal the current `versionCode()`, the record SHALL be discarded and wiped on load, and the device SHALL re-detect from scratch (the safety valve — every new build re-probes). There SHALL be no QML/Settings-UI path to view or clear it; the MCP reset is the only in-session operator path.

#### Scenario: Genuine classification persists across restart within the same build
- **WHEN** detection sets the latch from a real trigger and the application is restarted on the **same build**
- **THEN** the persisted record SHALL still be set
- **AND** both the scale and DE1 links SHALL come up at BALANCED on the first connect with no detection window

#### Scenario: New build re-detects (build-scoped safety valve)
- **WHEN** the application is restarted on a **different build** (`versionCode()` changed) with a latch persisted by a prior build
- **THEN** the persisted record SHALL be discarded and wiped on load
- **AND** the device SHALL request HIGH and re-run detection as if seen for the first time

#### Scenario: Never persisted without a trigger
- **WHEN** a device runs normally and detection never fires
- **THEN** nothing SHALL be persisted
- **AND** every run SHALL start at HIGH on both links

#### Scenario: Mis-classified capable device self-recovers
- **WHEN** a transient causes a one-off latch on capable hardware and it persists
- **THEN** it SHALL be cleared automatically on the next build, OR immediately via the MCP reset, whichever comes first
- **AND** there SHALL be no QML/Settings-UI path to clear it (by design)

#### Scenario: Internal persistence, no UI surface
- **WHEN** the classification is persisted
- **THEN** it SHALL be stored as an internal settings record with no `Q_PROPERTY`/QML/Settings-UI binding
- **AND** it SHALL be readable and resettable only via the MCP surface (diagnostic read + reset)
