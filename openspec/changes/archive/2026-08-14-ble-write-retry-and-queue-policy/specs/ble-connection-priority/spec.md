## MODIFIED Requirements

### Requirement: Scale-Feed Recovery Is Observable

The system SHALL emit an event-based recovery signal when a stalled scale feed resumes: after `WeightProcessor` has signalled an in-cycle scale-feed stall, the first subsequent genuine weight sample MUST emit a resume event carrying the stall gap duration. A DE1-fault-cluster window that elapses without reaching the fire threshold MUST be logged (observe mode) as the cluster subsiding. No timer may be used to detect recovery — it MUST be driven by the sample/window edge.

The weight a write-failed cascade contributes to the DE1-fault-cluster threshold SHALL be derived from what that cascade actually represents under the current retry budget, and MUST NOT be stated as a constant tied to a budget that has changed. The existing weighting treats a cascade as two faults on the reasoning that a ten-retry cascade is several seconds of sustained write starvation; a shorter budget makes a cascade both briefer and more frequent, so both the weight and the resulting fire rate MUST be re-derived rather than inherited. A single write failure MUST NOT reach the fire threshold on its own unless it genuinely represents sustained starvation.

#### Scenario: Stalled feed recovers on its own
- **WHEN** a scale-feed stall has been signalled during an extraction/preheat cycle
- **AND** a genuine (non-spike) weight sample subsequently arrives
- **THEN** a resume event is emitted exactly once on that stall→sample edge, carrying the elapsed gap
- **AND** in observe mode a WARN line records the recovery (e.g. "feed RESUMED after X.X s — would-have-been-backoff recovered at HIGH")

#### Scenario: Fault cluster subsides without escalation
- **WHEN** in observe mode a DE1-fault-cluster window elapses without reaching the fire threshold
- **THEN** a log line records that the cluster subsided without escalation

#### Scenario: Recovery signal does not alter SAW
- **WHEN** the resume signal is emitted
- **THEN** stop-at-weight, flow-rate, and per-frame-exit decisions are byte-identical to a run without the signal (recovery is observation only)

#### Scenario: The cascade weighting matches the retry budget in force
- **WHEN** the per-write retry budget changes
- **THEN** the fault weight assigned to a write-failed cascade is re-derived from the starvation that cascade now represents
- **AND** the rate at which the fire threshold is reached on a given real-world fault pattern is not increased merely because cascades became shorter and more frequent

## ADDED Requirements

### Requirement: The backoff trigger is calibrated against observed fault patterns, not restated constants

Any constant governing when the connection-priority backoff fires SHALL be traceable to an observed fault pattern rather than to another constant. A weighting expressed in terms of a retry count is coupled to that retry count, and the coupling SHALL be recorded where the weighting is defined so a later change to either is not made in ignorance of the other.

#### Scenario: A latch that demotes every scale for the session

- **WHEN** a single DE1 write failure would be sufficient to latch skip-HIGH app-run-wide
- **THEN** that sufficiency is justified against the fault pattern it is meant to detect, and recorded where the weighting is defined
