# Tasks — Decaid-matching limited Transport follow-up

The user explicitly requested the limited Decaid-matching path on October 8, 2026, replacing the earlier native-support-only capability proposal.

## 1. Existing handler and implementation

- [x] 1.1 Compare Decaid's maintenance preparation with the existing Decenza handler; confirm the shared 1356 boundary and unknown-build preparation at Decaid revision 94501c455d81fa9a31b4ce165bb0b0777e7beec6, unified_de1.dart:339-400. Decenza keeps its event-driven wait instead of the fixed delay.
- [x] 1.2 Remove Transport's Ready gate, allow connected idle/heating/ready, retain simulation and sleep/disconnection/other-operation gating; request selected-profile restoration on leaving or covering the page, deferring until the device is idle if another operation replaces Transport (corrected in review). No new firmware capability was required.
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

## PR #2034 review follow-up — deferred purge cancellation

Codex finding discussion_r4224757095 identified that an old/unknown-firmware GHC start remains deferred while the page still offers Back. Restoring the profile without cancelling that request lets a later state notification start AirPurge after exit.

- [x] R.1 Add an idempotent cancelPendingAirPurge slot scoped to AirPurge; invoke it before explicit exits, on StackView deactivation and at destruction before profile restoration. Disconnect clears deferred maintenance intent from the lost connection.
- [x] R.2 Add production-path regression coverage for old/unknown cancellation followed by Ready, a later explicit Start, preservation of another pending maintenance operation, and disconnect cleanup for AirPurge/Descale/Clean. Test results remain pending below.
- [x] R.3 Run the affected target and full suite through Qt Creator with the user's renewed permission; record results and read PR checks after push.
- [x] R.4 Update and verify the short wiki cancellation rule; published wiki commit 4492d9e and verified its rendered sentence.

Review validation: Qt Creator run 1791394637066 passed the affected target without warnings. Full run 1791394637067 passed 121/121 in 28230 ms, with zero failures/skips/test warnings. The matched startup project was Decenza-Desktop on codex/openspec-closeout; the user renewed permission to use Qt Creator for this fix. All 180 specs/changes pass strict validation; log-marker, translation-key, MCP-budget, QML statement and whitespace checks pass. These tests exercise the production cancellation/ready-notification paths through MockTransport; no physical machine was drained.

## Review workflow follow-up

- Push this correction to PR #2034 and resolve the addressed thread after checks pass.

## PR #2034 second review follow-up

- [x] R.5 Cancel old/unknown-GHC deferred AirPurge on competing device states before state observers can navigate, including replacement operations still heating.
- [x] R.6 Restore on deactivation as well as destruction; queue restoration quietly during active operations, checking the raw device state so Steam warm-up cannot be mistaken for idle heating. Repeated cleanup is idempotent until the next explicit Start.
- [x] R.7 Add production-path regressions for competing states, safe deferred restoration, covered idle/heating restoration and disconnect cleanup.
- [x] R.8 Run affected and full Qt Creator tests and record results; publish and verify the updated manual. Head checks must pass before resolving Codex threads.

Second-review validation: affected Qt Creator run 1791394637069 passed both targets in 4.73 s. Full run 1791394637070 passed 121/121 in 28.12 s, with zero failures, skips or test warnings. App build 1791394635059 succeeded without errors or warnings. All 180 specs/changes pass strict validation and source text-invariant checks pass. Wiki commit 83d2a43 is published and the rendered Transport sentence is verified. The initial new restoration tests incorrectly assumed synchronous phase notifications; they now wait for the actual queued notification, and expect the intentional disconnect warning. MockTransport verifies requested-state ordering and restored 93°C profile bytes; no physical machine was drained.


## PR #2034 final review scope

- [x] R.9 Distinguish unknown GHC status from confirmed no-GHC hardware for cold preparation; reset hardware confirmation on disconnect. Keep general start controls permissive.
- [x] R.10 Remove the espresso-overlap guard, its tests and its spec/manual requirements at the user’s instruction. The deliberate hardware-button overlap does not justify extra guards in the brew/upload paths.
- [x] R.11 Verify the remaining GHC fix and previous cancellation/restoration behavior through Qt Creator and strict spec validation; publish the corrected manual. Read head checks before PR handoff.

Final-scope validation: Qt Creator run 1791394637074 passed 121/121 in 28.93 s with zero failures, skips or test warnings. All 180 specs/changes pass strict validation; source text checks and whitespace checks pass. The remaining change relative to 23b5f4c8 is four net runtime lines for GHC confirmation, with tests for unread hardware, confirmed no-GHC hardware and reset on disconnect. The espresso guard and its supporting requirements/tests are removed. Wiki correction b1947f0 restores the earlier Transport guidance. No physical drain or hardware-button overlap was tested.
