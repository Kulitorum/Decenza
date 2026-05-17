## ADDED Requirements

### Requirement: DE1 Link Always Requests High Priority

The system SHALL request `CONNECTION_PRIORITY_HIGH` on the DE1 BLE link unconditionally after the DE1 controller connects, independent of device classification or Android version.

#### Scenario: DE1 connects
- **WHEN** the DE1 controller reaches the connected state
- **THEN** the system SHALL issue a connection-parameter update requesting HIGH priority on the DE1 link
- **AND** this request SHALL NOT be gated by Android SDK version or by the scale device classification

### Requirement: Scale Link Requests High Priority Unless The App-Run Skip-HIGH Latch Is Set

The system SHALL request `CONNECTION_PRIORITY_HIGH` on the scale BLE link after the scale controller connects, independent of Android SDK version and without consulting any persisted state, **except** when the app-run skip-HIGH latch is set (because detection triggered a backoff earlier this app run) — in which case it SHALL skip the HIGH request and come up at BALANCED. The latch is held on the process-lifetime `BLEManager` singleton (not on the per-scale transport), is shared by every scale this run, is in-memory only, and is never stored to disk/settings.

#### Scenario: Scale connects, no prior trigger
- **WHEN** the scale controller reaches the connected state AND the app-run skip-HIGH latch is not set
- **THEN** the system SHALL request HIGH priority on the scale link
- **AND** this request SHALL NOT be gated by Android SDK version
- **AND** the decision SHALL NOT depend on any persisted state

#### Scenario: Reconnect after a backoff stays at BALANCED (intended)
- **WHEN** the scale auto-reconnects after detection set the app-run skip-HIGH latch
- **THEN** the system SHALL NOT request HIGH and SHALL come up at BALANCED
- **AND** detection SHALL NOT re-arm (no reconnect loop)

#### Scenario: A different scale connected later this run also skips HIGH
- **WHEN** the app-run skip-HIGH latch is set AND the user connects a different scale (a scale-type change builds a fresh transport + detector)
- **THEN** the new scale's transport SHALL also skip HIGH and come up at BALANCED without re-running detection
- **AND** the contention SHALL NOT be re-paid per scale (it is a device-level property, not a per-scale one)

#### Scenario: Fresh app run starts from HIGH
- **WHEN** the application is restarted (the latch lives on the in-memory BLEManager singleton, so it is clear)
- **THEN** the scale SHALL again request HIGH and re-run detection from scratch

### Requirement: DE1 Error Cluster Is The Primary Fault Signal

The system SHALL classify the current session as dual-HIGH-incapable when, within a bounded watch window opened by the scale's HIGH-priority request, the DE1 link reports a clustered error condition (at least a threshold count of DE1 `CharacteristicWriteError`, a DE1 write "failed after retries" cascade, or a DE1 controller `ConnectionError`). This is the primary trigger because it is the fastest and is causally tied to the scale joining the radio. Detection state SHALL be scoped to the current connection only.

#### Scenario: DE1 errors cluster shortly after scale goes HIGH
- **WHEN** the scale link has requested HIGH priority AND the DE1 link reports DE1-side errors meeting the configured count threshold within the watch window
- **THEN** the system SHALL treat the current session as dual-HIGH-incapable and proceed to back off, before any shot where possible

#### Scenario: DE1 watch window only arms after a scale connects
- **WHEN** no scale is connected (no scale-HIGH request has been issued)
- **THEN** the DE1 error watch window SHALL NOT be armed and DE1 errors SHALL NOT trigger any scale priority change

### Requirement: Scale Weight-Feed Liveness Is The In-Shot Backstop

The system SHALL additionally treat a connected scale failing to deliver weight notifications at its expected cadence **while extraction is active and weight should be changing** as a dual-HIGH-incapable trigger for the current session, even if no DE1 error has been observed. This liveness evaluation SHALL be gated on a "weight should be flowing" condition (the DE1 in a pour / active-extraction state) so that a legitimately idle scale, or the absence of a scale, is never treated as a fault. It SHALL be evaluated in the scale-agnostic extraction / weight-processing path so it applies to any connected scale type, and SHALL NOT depend on any individual scale driver's internal liveness mechanism. It is the safety net for sessions where the DE1 cluster does not appear early.

#### Scenario: Scale stalls during active extraction with no early DE1 cluster
- **WHEN** extraction is active and weight should be changing AND the connected scale stops delivering weight notifications beyond the reconnect-gap threshold AND no qualifying DE1 cluster occurred earlier in the session
- **THEN** the system SHALL treat the current session as dual-HIGH-incapable and proceed to back off

#### Scenario: Idle scale is not a fault
- **WHEN** the scale is connected but extraction is not active (between shots, nothing changing on the platter) and the scale is quiet
- **THEN** the system SHALL NOT treat the quiet feed as a fault and SHALL NOT trigger a backoff

#### Scenario: No scale attached
- **WHEN** no scale is connected
- **THEN** the detection and backoff logic SHALL be inert and SHALL NOT trigger any priority change

### Requirement: Capable Device Is Never Backed Off

The system SHALL leave the scale link at HIGH for the session when neither fault signal fires.

#### Scenario: Capable device shows neither fault
- **WHEN** the watch window elapses without DE1 errors meeting the threshold AND the scale delivers weight normally whenever weight should be flowing
- **THEN** the system SHALL NOT trigger a backoff
- **AND** the scale link SHALL remain at HIGH priority for the session

### Requirement: Backoff Via Skip-HIGH Flag And Self-Reconnect

Upon a dual-HIGH-incapable trigger while the scale is connected at HIGH, the system SHALL set the app-run skip-HIGH latch on the `BLEManager` singleton and initiate a scale disconnect that flows into the existing scale auto-reconnect path. On the resulting reconnect the system SHALL NOT request `CONNECTION_PRIORITY_HIGH` on the scale link (taking the same skip behavior as the existing low-SDK path), so the link comes up at the platform-default BALANCED interval. The system SHALL trigger the backoff at most once per app run (it MUST NOT repeatedly bounce the connection for the same trigger, and once the latch is set no scale re-triggers it).

#### Scenario: Trigger sets the app-run latch and self-reconnects at BALANCED
- **WHEN** a dual-HIGH-incapable trigger occurs and the scale link is currently at HIGH
- **THEN** the system SHALL set the app-run skip-HIGH latch (BLEManager), disconnect the scale, and rely on the existing auto-reconnect
- **AND** the reconnected scale link SHALL NOT request HIGH and SHALL operate at the platform-default BALANCED interval

#### Scenario: No reconnect loop and no re-trigger by any scale
- **WHEN** the scale has reconnected with the app-run latch set
- **THEN** the system SHALL NOT request HIGH again for the remainder of the app run, for this or any other scale
- **AND** because the watch window only arms when HIGH is requested, detection SHALL NOT re-arm and SHALL NOT trigger another backoff

### Requirement: Backoff Confirmation Is Structural

The system SHALL NOT implement a connection-parameter-update subscription or a weight-resumption confirmation state machine. Correctness SHALL follow by construction: after the self-reconnect the scale is no longer at HIGH, so the dual-HIGH contention cannot recur for the session.

#### Scenario: Residual hardware failure is surfaced, not hidden
- **WHEN** a scale still fails to deliver weight even after reconnecting at BALANCED
- **THEN** the system SHALL surface this through the existing scale-disconnect / debug-log paths
- **AND** SHALL NOT silently suppress or mask the condition

### Requirement: App-Run Scoped, Not Persisted

The system SHALL NOT write any dual-HIGH classification to disk, settings, or any other persistent store. The skip-HIGH latch is in-memory and app-run-scoped, held on the `BLEManager` singleton: it deliberately survives the backoff-induced reconnect AND any scale-type change / fresh transport for the remainder of the run (the contention is a device-level property, not a per-scale one), but SHALL NOT survive an application restart, which SHALL start from HIGH and re-run detection.

#### Scenario: Latch survives the backoff reconnect (intended)
- **WHEN** detection sets the app-run latch and the scale auto-reconnects within the same run
- **THEN** the latch SHALL still be set on the BLEManager singleton
- **AND** the reconnected link SHALL come up at BALANCED

#### Scenario: Latch survives a scale-type change (intended)
- **WHEN** the latch is set and the scale is later replaced by a different scale type (old transport destroyed, fresh one built)
- **THEN** the fresh transport SHALL observe the latch and come up at BALANCED without re-running detection

#### Scenario: Nothing survives an app restart
- **WHEN** the application is restarted
- **THEN** no stored state about the prior outcome SHALL exist (the BLEManager latch is in-memory)
- **AND** the scale SHALL request HIGH and re-run detection as if seen for the first time

#### Scenario: False-positive is non-sticky
- **WHEN** a transient hiccup on capable hardware causes a one-off backoff in a session
- **THEN** the cost SHALL be limited to that session running the scale at BALANCED
- **AND** the next app run SHALL return to HIGH automatically with no manual reset
