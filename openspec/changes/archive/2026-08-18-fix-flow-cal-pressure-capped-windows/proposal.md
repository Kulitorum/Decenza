## Why

Auto flow calibration's flow-branch formula (`MainController::computeAutoFlowCalibration()`,
`src/controllers/maincontroller.cpp:3372`) computes the calibration ideal as
`meanWeightFlow / (targetFlow * density)` — using the profile's stated TARGET flow, never the
flow the DE1 actually achieved during the window. This assumes the pump held target flow. On
flow frames that carry a pressure ceiling (D-Flow, D-Flow/Q, and similar profiles that pour
flow-controlled but cap pressure), the pump frequently can't hold target flow against a
resistant puck, and the window is still routed through the flow-branch formula. The resulting
ideal measures nothing about sensor accuracy — it's a garbage value that gets accumulated into
the same 5-shot batch median as legitimate target-met windows.

This was analyzed 2026-08-02 and deliberately left unfixed for lack of user-felt harm (see
project memory `project_flow_cal_target_flow_assumption`). That evidence has now arrived:
[Kulitorum/Decenza#1823](https://github.com/Kulitorum/Decenza/issues/1823) reports a user's
hand-tuned D-Flow calibration (1.35, a good mass/volume flow match) auto-degraded to 1.01 (a poor
match) after 5 shots. Debug-log analysis of the attached system log across 8 real windows on that
profile confirmed the exact bimodal split predicted: target-achieved windows cluster at ideal
≈1.03, target-missed windows (5 of 8, machine flow reaching only 57-67% of target because the
frame's pressure limiter — 8.5 bar in that profile — is holding) scatter to ideal ≈0.75-0.90,
dragging the batch median down. Cross-checked against this repo's own DE1 running D-Flow/Q: every
sampled window currently achieves ~99-101% of target flow, so the bug is dormant there — proving
the failure is workload-dependent (target-achieved vs. pressure-capped/target-missed), not a
universal defect.

## What Changes

- `computeAutoFlowCalibration()`: for a window classified flow-controlled, if the window's
  measured `meanMachineFlow` deviates from the frame's target flow by more than a threshold,
  treat the window as pressure-capped and route it through the pressure-branch formula
  (`currentMultiplier * weightFlow / (machineFlow * density)`, which uses actually-achieved flow
  and is independent of whether target was reached) instead of the flow-branch formula that
  assumes target was hit.
- Window-ratio sanity guard for flow-classified windows: stop comparing weight flow against the
  profile's target flow when the window has been re-routed to the pressure formula — compare
  against measured machine flow instead, consistent with the pressure branch's own guard.
- Existing per-profile behavior on target-achieved windows (the common case, including every
  window sampled on this repo's own DE1) is unchanged — this only changes handling of windows
  where target flow was not reached.
- Log the reclassification decision (achieved vs. target flow, threshold, which formula was
  used) so the failure mode is diagnosable from a submitted debug log without re-deriving it by
  hand, per `docs/CLAUDE_MD/LOGGING.md` conventions.
- One-time migration clears pending per-profile flow-cal batches (`calibration/flowCalBatch`)
  only — not the stored multipliers themselves — so no batch median mixes ideals computed under
  the old (target-flow) and new (achieved-flow-aware) formulas. Exact mechanics in `design.md`.
- Update `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` to document the achieved-flow deviation check
  as part of window-level classification.

## Capabilities

### New Capabilities
- `auto-flow-calibration`: per-profile flow calibration algorithm — steady-state window
  detection, window-level flow/pressure classification (including the achieved-vs-target flow
  deviation check this change adds), ideal computation formulas, batched median updates, and
  sanity bounds. No spec currently exists for this capability; this change introduces it,
  including the corrected behavior below.

### Modified Capabilities
_(none — no existing spec currently documents auto flow calibration behavior)_

## Impact

- `src/controllers/maincontroller.cpp` — `computeAutoFlowCalibration()`, window ratio guard
- `src/controllers/autoflowcalclassifier.{h,cpp}` — window classification (may need to expose
  achieved-flow deviation, or that's already available via `meanMachineFlow`)
- `src/core/settings_calibration.{h,cpp}` — pending-batch clear on migration, if the reset is
  implemented as a versioned settings migration
- `src/core/settingsserializer.cpp` — if the migration touches serialized settings import/export
- `tests/tst_autoflowcal.cpp` — new coverage for the pressure-capped/target-missed path
- `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md` — documentation of the corrected algorithm
- No MCP surface changes expected (`get_flow_calibration`/`set_flow_calibration` behavior is
  unaffected — this only changes how the auto-computed ideal is derived, not the tools around it)
