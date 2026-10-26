## 1. Implementation

- [x] 1.1 Report `connect-failed` from `BleTransport::onControllerError` when the attempt never reached Connected
- [x] 1.2 Track in `BLEManager` whether a DE1 advertised since the last connect attempt ended
- [x] 1.3 Skip uncorroborated `connect-failed` faults in `QtScaleBleTransport::onDe1LinkFault`
- [x] 1.4 Correct the wedge-detector comments that claimed an absent DE1 produces no fault

## 2. Validation

- [x] 2.1 Classify the #1176 reporter's DE1 controller errors (dropped link vs failed attempt, DE1 advertising or not) and today's SM-X210 log against the rule
- [x] 2.2 Extend `tst_bletransporterror` to assert both fault kinds
- [x] 2.3 Full suite green through Qt Creator (207/207 on a build containing these edits)
- [ ] 2.4 On the tablet in observe mode: power-cycle the DE1 with the scale connected and confirm no `WOULD back off (trigger=de1-fault-cluster)` line (skipped by maintainer decision 2026-10-26, not run; watch for it in the beta)
