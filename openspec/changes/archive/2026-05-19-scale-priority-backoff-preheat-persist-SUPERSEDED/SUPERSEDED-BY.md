# SUPERSEDED

Superseded by change `reconcile-scale-priority-backoff-model`
(archived `2026-05-19-reconcile-scale-priority-backoff-model`).

**Status:** archived WITHOUT applying this change's spec deltas. Its deltas
encode the original single-cause framing (dual-HIGH contention as *the*
#1176 cause) and the mid-shot scale-bounce remediation; applying them would
corrupt the corrected canonical `ble-connection-priority` spec.

**Why superseded (vindicated but refined):**
- Core thesis VINDICATED — genuine dual-HIGH BLE radio contention on weak
  hardware is real and confirmed (SM-X200 / Tab A8 logs in #1176; Teclast
  P80X log in #1093); backing the scale link off to BALANCED mitigates it.
- REFINED — #1176 has TWO independent root causes: (1) the long-standing
  `ScaleDevice::setWeight()` value-dedup starving the weight pipeline during
  static windows (fixed by #1224, priority-independent), and (2) the genuine
  contention above (the latch's job; #1224 is necessary but not sufficient
  on weak hardware).
- The mid-shot disconnect/reconnect remediation was harmful and was replaced
  by latch-only-while-a-shot-is-in-progress + idle-only-bounce (#1226).

The corrected, canonical behavior now lives in
`openspec/specs/ble-connection-priority/spec.md`.
