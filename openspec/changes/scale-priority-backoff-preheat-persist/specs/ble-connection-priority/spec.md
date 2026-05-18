## MODIFIED Requirements

### Requirement: Scale Weight-Feed Liveness Is The In-Shot Backstop

The system SHALL additionally treat a connected scale failing to deliver weight notifications at its expected cadence **whenever weight is expected to stream** as a dual-HIGH-incapable trigger for the current app run, even if no DE1 error has been observed. "Weight is expected to stream" SHALL cover three gated conditions, not extraction alone: (a) the pre-shot **EspressoPreheating** phase (the espresso cycle has started, the scale is connected, tare is complete) so the stall can be caught during warm-up and the backoff + reconnect can begin before the pour; (b) **active extraction** (the existing in-shot backstop); and (c) an **active startup connection-priority probe window** (see the Startup Connection-Priority Probe requirement) so a probe-provoked stall trips the same detector at idle, before any shot. The liveness evaluation SHALL remain gated so that a legitimately idle scale with no espresso cycle in progress and no probe window active, or the absence of a scale, is never treated as a fault. It SHALL be evaluated in the scale-agnostic extraction / weight-processing path so it applies to any connected scale type, and SHALL NOT depend on any individual scale driver's internal liveness mechanism. The skip-HIGH latch it sets remains the in-memory, app-run-scoped `BLEManager` latch (no persistence change in this capability).

#### Scenario: Scale stalls during preheating (caught before the pour)
- **WHEN** the espresso cycle has started (machine in EspressoPreheating), the scale is connected and tare is complete, AND the scale stops delivering weight notifications beyond the reconnect-gap threshold
- **THEN** the system SHALL treat the device as dual-HIGH-incapable and proceed to back off during preheating, before extraction starts where the preheat duration allows

#### Scenario: Scale stalls during active extraction (backstop still applies)
- **WHEN** extraction is active and weight should be changing AND the connected scale stops delivering weight notifications beyond the reconnect-gap threshold AND no qualifying DE1 cluster occurred earlier in the run
- **THEN** the system SHALL treat the device as dual-HIGH-incapable and proceed to back off

#### Scenario: Scale stalls during the startup probe window (caught at idle)
- **WHEN** the startup connection-priority probe window is active and the scale stops delivering weight notifications beyond the reconnect-gap threshold
- **THEN** the system SHALL treat the device as dual-HIGH-incapable and proceed to back off during idle, before any shot

#### Scenario: Idle scale outside any gated window is not a fault
- **WHEN** the scale is connected but no espresso cycle is in progress (neither preheating nor extracting) and no probe window is active, and the scale is quiet
- **THEN** the system SHALL NOT treat the quiet feed as a fault and SHALL NOT trigger a backoff

#### Scenario: No scale attached
- **WHEN** no scale is connected
- **THEN** the detection and backoff logic SHALL be inert and SHALL NOT trigger any priority change

## ADDED Requirements

### Requirement: Startup Connection-Priority Probe

The system SHALL, once per scale connect, run a bounded read-only DE1 traffic probe that opportunistically provokes the dual-HIGH BLE contention while the scale-feed-liveness detector is armed, so a weak device is detected and backed off **during idle, before any shot**. The probe SHALL be strictly additive: when it provokes nothing it SHALL be a complete no-op with behaviour identical to the probe being absent, and overall correctness SHALL NOT depend on the probe (the preheat/extraction liveness net is the guarantee whether or not the probe ever provokes).

The probe SHALL only start after the scale is **confirmed correctly streaming weight** (a healthy run of weight notifications at the expected cadence has established a known-good baseline), with the DE1 connected and the machine idle — never on bare scale-connect. The probe stressor SHALL be **read-only by construction**: a burst of MMR block reads plus a read-poll of safe readable DE1 characteristics only. The probe SHALL NOT write or poll any state-affecting / DANGER characteristic, SHALL NOT command or reconfigure the machine, and SHALL respect the existing BLE command queue / write-spacing rules (enqueued normally, not bypassed). The probe SHALL yield immediately (end with no trip) if an espresso cycle starts while it is running, and SHALL run at most once per scale connect; once the app-run skip-HIGH latch is set the probe SHALL NOT run again.

#### Scenario: Probe provokes the contention on a weak device (pre-shot win)
- **WHEN** the scale is confirmed streaming, the DE1 is connected, the machine is idle, and the probe's read-only burst starves the weak shared radio so the scale feed stalls beyond the reconnect-gap threshold
- **THEN** the system SHALL treat the device as dual-HIGH-incapable via the existing liveness detector and back off + reconnect at BALANCED during idle, before any shot

#### Scenario: Probe provokes nothing (complete no-op)
- **WHEN** the probe runs and the scale feed does not stall
- **THEN** the probe SHALL end silently with no observable effect
- **AND** subsequent behaviour SHALL be identical to the probe not having run (the preheat/extraction liveness net remains the guarantee)

#### Scenario: Probe does not start until the scale is confirmed streaming
- **WHEN** the scale controller has just reached "connected" but has not yet delivered a healthy run of weight notifications
- **THEN** the probe SHALL NOT start
- **AND** it SHALL start only after the known-good streaming baseline is established (DE1 connected, machine idle)

#### Scenario: Probe never misconfigures the DE1
- **WHEN** the probe runs its stressor
- **THEN** it SHALL issue only reads (MMR block reads and safe readable-characteristic reads)
- **AND** it SHALL NOT write or poll any state-affecting / DANGER characteristic and SHALL NOT change machine state, so no profile re-upload or verification is required

#### Scenario: Probe is capable-hardware-safe
- **WHEN** the probe runs on a device with a capable radio that sustains the read burst without stalling the scale feed
- **THEN** the system SHALL NOT trigger a backoff
- **AND** the scale link SHALL remain at HIGH for the run (the "capable never backs off" invariant is preserved)

#### Scenario: Probe yields to a shot started mid-probe
- **WHEN** an espresso cycle starts while the probe window is active
- **THEN** the probe SHALL end immediately without itself triggering a backoff
- **AND** normal preheat/extraction liveness detection SHALL govern from that point

#### Scenario: Probe runs at most once per scale connect
- **WHEN** the probe has already run for the current scale connect, or the app-run skip-HIGH latch is already set
- **THEN** the probe SHALL NOT run again for that connect
