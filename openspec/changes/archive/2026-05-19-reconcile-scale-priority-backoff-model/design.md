## Context

`scale-ble-priority-backoff` (#1185, shipped) and `scale-priority-backoff-preheat-persist` (stale) describe #1176 as one cause — dual-HIGH BLE contention starving the scale link — fixed by detecting a stall and reconnecting the scale at BALANCED. The 2026-05-19 investigation refined this into **two independent causes**, both confirmed from GCDE-VER1's own SM-X200 / Tab A8 logs:

- **Cause 1 — weight-sample dedup (static windows).** `ScaleDevice::setWeight()` has always dropped unchanged values (`if (m_weight != weight)`, since the initial commit). During the static preheat/quiet window the tared weight is a repeated ~0, so `weightChanged` never fires and the weight pipeline (per-frame exit, SAW, stall detector) is starved. In build-3385 the weight-gated "Infusing" frame's first processed sample was `9.6 g` and it exited immediately (`FRAME-WEIGHT EXIT: weight 9.6 >= 2`). Priority-independent; confirmed across a 4-month / 3-era corpus. Shipped fix: **#1224** (unconditional `weightSampleReceived` feeds the pipeline; `weightChanged` stays deduped for UI/MQTT).

- **Cause 2 — genuine dual-HIGH radio contention (changing weight).** Distinct from cause 1 and **not** explainable by the dedup: in build-3385 (which contained *zero* backoff code) the cup physically held 9.6 g by 6.3 s into extraction while pressure climbed 0.32 → 1.15 → 3.17 bar (water being driven through the puck), yet **zero** samples were delivered in between. Weight rose monotonically through ~1…9 g — *distinct* values; the `!=` dedup only suppresses a value equal to the previous one and provably cannot drop a monotonic climb. Their absence is genuine BLE-notification loss under dual-HIGH on a weak radio. Build-3388: the same SM-X200, once the backoff reconnected it at BALANCED, delivered changing weight cleanly (~2 s cadence) to shot end. So BALANCED demonstrably resolves cause 2; HIGH on this radio does not.

#1226 separately established that the backoff must not tear the scale down mid-shot: in build-3388 the static-preheat gap tripped the detector, the backoff bounced the scale, and that bounce produced the user-visible "no scale — estimating" window. The remediation mechanism was harmful even though the latch decision was correct.

## Goals / Non-Goals

**Goals:**
- Canonical `ble-connection-priority` spec states #1176's two causes correctly: cause 1 fixed by #1224; cause 2 (genuine weak-HW dual-HIGH contention) mitigated by the BALANCED latch.
- Spec states the latch is **confirmed-warranted** for weak hardware, not defensive/unproven, and that #1224 is necessary-but-not-sufficient there.
- Spec keeps the #1226 rule (latch-only while a shot is in progress; bounce only when idle) and forbids device-model gating.
- The two stale changes are retired as SUPERSEDED without applying their (single-cause, mid-shot-bounce) deltas.

**Non-Goals:**
- No code change (shipped via #1224/#1226).
- No device allow/block list; no per-model gating. Detection remains runtime self-identifying.
- Not re-litigating whether genuine contention exists — it is confirmed. Remaining work is *tuning when the latch engages*, out of scope here.

## Decisions

- **Modify the existing `ble-connection-priority` capability** rather than create a new one — this corrects/extends the same capability's requirements.
- **Retire the two stale changes by `git mv` to `openspec/changes/archive/…-SUPERSEDED/`, NOT via `openspec archive`.** Their deltas encode the single-cause model and the mid-shot-bounce remediation; applying them would corrupt the corrected spec. A manual move with a `SUPERSEDED-BY` marker preserves history (and credits their vindicated core thesis: genuine contention → BALANCED) without applying stale deltas. Alternative (`openspec archive --skip-specs`) rejected — it implies "completed/applied", whereas the accurate status is "superseded and refined".
- **Keep the backoff/latch as a first-class, warranted mechanism in the spec — not "demoted to telemetry".** Earlier I framed the weak-HW latch as unproven; the Log-1/Log-2 evidence disproves that framing. Observe mode remains useful for *tuning* the engage point, but the latch's necessity for cause 2 is settled.

## Risks / Trade-offs

- [Over-aggressive latch on capable hardware] → Detection is runtime self-identifying (genuine sustained stall / fault cluster), epoch-scoped, and #1226 makes a mistaken engage non-destructive (no mid-shot bounce; BALANCED is proven safe and costs no bad coffee). No model list, so no static misclassification.
- [Spec implies the engage threshold is final] → Spec states the *existence* of cause 2 is confirmed but the engage tuning is an open observe-mode follow-up, so future tightening is a normal change, not a contradiction.
- [Readers think #1224 alone fixes #1176 everywhere] → Spec explicitly states #1224 is necessary-but-not-sufficient on weak hardware; cause 2 needs the latch.

## Decision

Ship what we have (#1224 + #1226 + the existing detector/latch) and **gather field experience before any further work**. This spec is reconciled and archived as-is; the items below are recorded as *possible futures*, explicitly **not committed** — they are revisited only if field evidence warrants. No code or detector changes are planned now.

## What We Know — detection assessment (from #1093 + #1176 logs)

Cross-checked the backoff detection against the genuine-weak-hardware logs:

- **DE1-fault-cluster trigger works.** #1093 (Teclast P80X, Android 9) broken log shows the exact qualifying pattern — ≥3 `CharacteristicWriteError FAILED after retries` + `CONTROLLER ERROR: ConnectionError` clustered in ~14 s (≈20 h into the session, on a profile-change write burst), comfortably inside `kDe1FaultThreshold=2` / `kDe1FaultWindowMs=20000`. Counts only write-failed/controller-error (transient retries excluded, per review fix 6.3), so capable HW is not false-tripped; the sliding per-fault window catches late clusters.
- **scale-feed-stall trigger works only post-#1224.** Pre-#1224 it was structurally blind to #1176's failure (`m_lastWallClockMs <= 0` gate; the blind window preceded the first processed sample — proven on build-3385). #1224 incidentally un-blinds it (constant-0.0 preheat now streams → clock primed → a genuine cause-2 mid-shot loss now grows the gap → SUSPECTED→CONFIRMED) **and** removes its false positives (SM-X210 #1224 build: `recentObserveEvents: []`).
- **#1093 and #1176 are one condition, two faces:** dual-HIGH starves whichever link loses arbitration (DE1 writes vs scale notify). A skip-HIGH→BALANCED latch from either trigger relieves both for subsequent runs.
- **Verdict:** detection should work in the field post-#1224/#1226. Residual risks are coverage/tuning, not "does it fire."

## Possible Future Improvements (deferred — field experience first; NOT committed)

1. **Weak-HW confirmation log.** Obtain an SM-X200 (Tab A8) #1224-build *observe-mode* log to confirm the scale-feed trigger actually fires on genuine cause-2 contention on weak hardware (good-HW data can't show this). Verification, not code.
2. **Cross-link synergy as an explicit, tested requirement.** Prove a latch from a scale-feed-stall also prevents the #1093-class DE1-write failures on the next run (and vice-versa).
3. **Confirm-threshold calibration.** Tune `kScaleStallConfirmMs` against real weak-HW contention gaps (Log-1's genuine loss was ~6 s, right at the threshold) using observe telemetry.
4. **Idle pre-shot probe.** Carry forward `scale-priority-backoff-preheat-persist`'s read-only idle-probe idea so a fresh weak device latches *before* shot 1 (epoch-persisted → once-ever), avoiding a degraded first shot.
5. **Decide #1097's fate.** #1093 was mitigated by #1097 (blunt skip-HIGH-on-scale for Android < 11), which contradicts "no device/OS blocklist, runtime self-identification." Once the runtime detector is field-trusted, decide whether #1097 stays as belt-and-suspenders or is retired (it currently force-BALANCEDs capable Android-<11 devices).

These remain in this archived change's record as the known follow-up surface; none is scheduled.
