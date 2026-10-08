# Tasks — Decaid-matching limited Transport follow-up

The user explicitly requested the limited Decaid-matching path on October 8, 2026, replacing the earlier native-support-only capability proposal.

## 1. Existing handler and implementation

- [x] 1.1 Compare Decaid's maintenance preparation with the existing Decenza handler; confirm the shared 1356 boundary and unknown-build preparation at Decaid revision 94501c455d81fa9a31b4ce165bb0b0777e7beec6, unified_de1.dart:339-400. Decenza keeps its event-driven wait instead of the fixed delay.
- [x] 1.2 Remove Transport's Ready gate, allow connected idle/heating/ready, retain simulation and sleep/disconnection/other-operation gating; restore the selected profile on exit only in idle/heating/ready phases. No C++ capability or backend change was required.
- [x] 1.3 Add five production-handler regression rows for unknown/1352/1356/1358 GHC firmware and no-GHC, checking preparation ACK, no premature AirPurge and release through the state-notification path. Qt Creator run 1791394637063 passed without warnings; the first fixture attempt exposed missing fake ACKs at teardown and was corrected.

## 2. Documentation and verification

- [x] 2.1 Reconcile proposal/design/delta to the Decaid-matching behavior and remove the obsolete main Purpose claim; strict validation passes. Wiki commit c3cc5dd documents cold starts and older-GHC preparation.
- [x] 2.2 Verify the Transport page in the matched live simulation app: Start, stop/abort, conditional completion and return/profile restoration; record any physical-machine check separately rather than equating simulation with a real drain.
- [x] 2.3 Run full Qt Creator suite before PR handoff: 1791394637064 passed 121/121 in 28530 ms, no failures/skips/test warnings.

## Workflow follow-up

- Archive after live UI evidence is recorded. A real cold-machine drain has not been performed during this work; the existing backend's five mocked firmware/hardware paths are tested, and the remaining physical observation must retain that distinction.

## Live UI evidence — October 8, 2026

The verified local Decenza-Desktop process (PID 79623, simulationMode=true) displayed the Transport prepare screen with Start enabled. Start entered Transport and displayed the running view; Stop returned to preparation without claiming empty. A second run completed after the simulator's 12-second duration and displayed the conditional guidance (only empty if it ran until no water came out; early stop may leave water). Done returned to the preceding Settings page. Current profile remained D-Flow / Q. The restoration call uses the normal uploader, which can defer behind an existing upload; no claim is made that a real preparation-profile/restore pair was observed in this simulator.

Five firmware/hardware rows verify the existing production maintenance backend with a MockTransport. No physical DE1 was drained during this close-out. Real-machine observation is a post-deployment follow-up, not reported as passed; the user requested reuse of Decaid's already-shipped policy rather than another native-capability implementation.
