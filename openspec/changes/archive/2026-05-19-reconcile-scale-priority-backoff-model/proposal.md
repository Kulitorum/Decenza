## Why

The active `scale-ble-priority-backoff` (#1185) and `scale-priority-backoff-preheat-persist` proposals describe #1176 as a single cause (dual-HIGH BLE contention) and a remediation that bounces the scale mid-shot. The 2026-05-19 investigation plus shipped #1224/#1226 showed #1176 has **two** distinct root causes and the mid-shot bounce is harmful. Those proposals are partly right (genuine contention IS real and confirmed on weak hardware) but incomplete and now inaccurate, so archiving them verbatim would leave the canonical specs wrong. The spec must state both causes correctly.

## What Changes

- Record that **#1176 has two independent root causes**, both confirmed from the reporter's SM-X200 (Tab A8) logs:
  1. **Weight-sample dedup.** `ScaleDevice::setWeight()`'s `if (m_weight != weight)` guard (present since the repo's initial commit) drops unchanged samples, starving the weight pipeline (per-frame weight-exit, SAW, stall detector) during *static* windows (preheat / quiet). Priority-independent. **Fixed by #1224** (unconditional `weightSampleReceived` feeds the pipeline; `weightChanged` stays deduped for UI/MQTT).
  2. **Genuine dual-HIGH BLE radio contention on weak hardware.** Confirmed: in build-3385 (zero backoff code) the cup reached 9.6 g by 6.3 s with **zero** samples delivered while pressure was building — a monotonic climb of *distinct* values that the `!=` dedup provably cannot suppress, i.e. genuine notification loss. In build-3388 the same device, once reconnected at BALANCED, delivered changing weight cleanly to shot end. **Not fixed by #1224.** Mitigated by the connection-priority skip-HIGH → BALANCED latch.
- Therefore: #1224 is **necessary but not sufficient on weak hardware**; the BALANCED latch is **warranted and confirmed-effective** for cause 2, not "defensive/unproven."
- Record **#1226**: the latch MUST NOT disconnect the scale mid-shot (that bounce itself caused the "no scale, estimating" failures); it latches always and applies BALANCED at the next natural reconnect, bouncing only when idle. This fixes the *mechanism*, not the *need* for the latch.
- Constrain the latch to **runtime self-identification only — no device model allow/block list.**
- **Supersede and retire** `scale-ble-priority-backoff` and `scale-priority-backoff-preheat-persist`: their core thesis (genuine contention → BALANCED) is vindicated, but their single-cause framing and mid-shot-bounce remediation are corrected here. Archive them as SUPERSEDED **without applying their deltas** (those deltas predate #1224/#1226 and the two-cause model).
- Spec/documentation reconciliation only — code already shipped (#1224, #1226). No new implementation.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `ble-connection-priority`: state #1176's two root causes; add the weight-sample-delivery requirement (#1224); state the BALANCED latch is the confirmed mitigation for genuine weak-HW dual-HIGH contention (#1224 necessary-not-sufficient); require latch-only-while-shot-active / idle-only-bounce (#1226); forbid device-model gating (runtime self-identification only).

## Impact

- Specs: `openspec/specs/ble-connection-priority/spec.md` (requirement deltas + accurate Purpose).
- Active changes retired: `scale-ble-priority-backoff`, `scale-priority-backoff-preheat-persist` → archived SUPERSEDED, deltas not applied.
- Code: none (shipped — #1224 `scaledevice.{h,cpp}` / `weightprocessor.*` / `main.cpp`; #1226 `qtscalebletransport.*` / `scalebletransport.h` / `main.cpp`).
- Open follow-up (tuning, not existence): observe-mode telemetry to refine when the genuine-contention latch should engage on weak hardware. The contention itself is no longer in question.
