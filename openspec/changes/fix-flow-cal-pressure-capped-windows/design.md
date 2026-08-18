## Context

All of the relevant logic lives in `MainController::computeAutoFlowCalibration()`
(`src/controllers/maincontroller.cpp:3016-3400`, called from `MainController::onShotEnded` via
line 4006). Window classification is a separate step in `AutoFlowCalClassifier`
(`src/controllers/autoflowcalclassifier.{h,cpp}`), which walks the shot's `PhaseMarker` stream to
decide which frames a steady-state window touched and returns `isFlowProfile`, `targetFlow`, and
the touched frame range. `computeAutoFlowCalibration()` then picks a formula based on
`isFlowProfile` alone — it has no path that reconsiders the classification against what the window
actually measured. See `proposal.md` for the failure this produces and the evidence from
[#1823](https://github.com/Kulitorum/Decenza/issues/1823) plus this repo's own DE1.

Existing constants relevant to the fix (`maincontroller.cpp:3070-3100`): `kMaxWindowRatio = 1.35`,
`kMinWindowRatio = 0.75`, `kWaterDensity93C = 0.963`. Settings storage:
`calibration/perProfileFlow` (applied multipliers) and `calibration/flowCalBatch` (pending
ideals), both JSON maps keyed by profile filename, managed by `SettingsCalibration`
(`src/core/settings_calibration.{h,cpp}`). Precedent for a formula-change migration exists twice
already in `src/core/settings.cpp` (`calibration/v2RatioGuardReset`,
`calibration/v3FlowProfileReset`) — both do a full reset (`resetAllProfileFlowCalibrations()` +
`setFlowCalibrationMultiplier(1.0)`), because in both those cases the *stored multipliers
themselves* were shown to be corrupted by the prior formula.

## Goals / Non-Goals

**Goals:**
- Stop computing nonsense ideals for flow-controlled windows that didn't reach target flow, by
  falling back to the achieved-flow (pressure-branch) formula.
- Leave the common case — windows that do reach target flow, which is every window sampled on
  this repo's own D-Flow/Q — numerically unchanged.
- Make the fallback decision visible in the debug log without requiring source-level analysis to
  reconstruct it (this change was only diagnosable this time by grepping raw log lines and
  recomputing the formula by hand).
- Avoid discarding calibration progress that isn't actually wrong: unlike the v2/v3 migrations,
  this defect is per-window, not systemic — most existing per-profile multipliers were reached by
  batches containing a mix of good and bad windows, with the batch median's outlier rejection
  already partially absorbing the bad ones (this is explicitly why the prior "leave it alone"
  decision held for as long as it did — see `project_flow_cal_target_flow_assumption`).

**Non-Goals:**
- Not attempting to detect or correct a pressure cap ahead of time (e.g. from profile JSON) —
  detection stays purely telemetry-based (measured vs. target flow), consistent with how the rest
  of the algorithm already works from telemetry rather than static profile inspection.
- Not changing the steady-state window *selection* step, the stream-force rejection check, or the
  sanity clamp bounds — only the formula choice and its associated ratio guard for
  already-selected, already-classified flow windows.
- Not resetting already-applied per-profile or global multipliers. Only pending batch accumulators
  are cleared (see Migration Plan).
- Not adding a user-facing setting for the deviation threshold — per [[feedback_minimize_settings]],
  this is an internal algorithm constant, not a dial for the user to turn.

## Decisions

### Deviation threshold: 10%, compared against the touched frame's target flow, undershoot only

The proposal's originally-scoped fix (from the 2026-08-02 analysis already on record) used a 10%
deviation threshold. Evidence from #1823 supports this: target-achieved windows there measured
99-101% of target machine flow (1.69-1.70 against 1.7, 1.69 against 1.8 on this repo's DE1);
target-missed windows measured 57-67% of target. There is no observed case sitting near the
boundary, so the exact threshold is not sensitive to this data, but 10% is chosen because it's
tight enough to still catch genuine sensor-error windows near target (which is the case the
flow-branch formula exists to measure) while being comfortably outside the noise band the
same-frame samples show session to session (≤2% at target — e.g. 1.82 vs 1.83 vs 1.90 across three
of this repo's own shots).

**Corrected during review to undershoot-only** (initial implementation checked deviation in either
direction — an oversight, not a considered choice, caught by `/code-review`). A pressure ceiling
can hold flow below its setpoint; it has no mechanism to push flow above one, so an overshoot
reading has no pressure-cap explanation and the mechanism this check exists to detect cannot
produce it. Reclassifying an overshoot window anyway would route it through the achieved-flow
formula's reported-flow denominator on a window that may still be genuinely PID-locked to target —
structurally the same shape as the v2 feedback-loop bug (`ideal = factor * weightFlow /
(reportedFlow * density)`, which drove a factor from 1.0 to 0.59 over 30 shots because reported
flow on a flow-controlled window is pinned near target independent of the factor). `deviation >
threshold` is therefore gated on `meanMachineFlow < targetFlow` as well.

**Alternative considered:** compare against the *existing* window ratio guard bounds
(`[0.75, 1.35]`) instead of a separate threshold — i.e., let the ratio guard's existing rejection
also decide the formula. Rejected: the ratio guard's job is "is this window's data trustworthy at
all", and its bounds are deliberately wide (per-sample bounds are even wider, `[0.4, 2.5]`) to
tolerate scale noise. Using those same wide bounds to decide *which formula computes physics*
would let windows at 75% of target still use the target-flow formula, which is close to the
worst-observed target-missed windows (57-67%) and would still produce a materially wrong ideal.
The two checks answer different questions and should stay separate constants.

### Re-routed windows use the pressure-branch formula and its own ratio guard, not a fresh third formula

The pressure-branch formula (`currentMultiplier * weightFlow / (machineFlow * density)`) already
exists and already does exactly what's needed for a window that didn't reach target: it uses
*achieved* flow, is validated on pressure profiles today, and its own ratio guard
(`windowRatio` against `[kMinWindowRatio, kMaxWindowRatio]`) is already computed unconditionally
in the current code (the `else` branch at `maincontroller.cpp:3357`) — reusing it for re-routed
flow windows is a formula selection change, not new guard logic.

**Alternative considered:** a dedicated formula for "flow-controlled but pressure-capped" windows,
e.g. weighting between the two formulas by how close measured flow came to target. Rejected as
unjustified complexity — the physics reasoning for the pressure-branch formula (recover raw sensor
flow by dividing out the current multiplier from reported flow) is equally valid regardless of
which frame type set the pump's behavior; a capped flow frame's pump is, in that moment,
constrained by the pressure limiter the same way a pressure-controlled frame's pump is constrained
by its pressure target. There's no evidence a blended formula would produce a better estimate, and
it adds a knob nobody asked for.

### Migration: clear pending batches only, not applied multipliers

Both existing formula-change migrations (v2, v3) did a full reset because the prior formula was
shown to systematically corrupt the *stored* multiplier (dragging it toward ~0.6 or drifting it
via feedback loop). This defect is different in kind: it corrupts individual *ideals* fed into a
batch that already applies median-based outlier rejection across 5 shots. The reporter's own data
shows the corrupted batch was still bounded (0.755-1.230, not a runaway value), and this repo's
own D-Flow/Q multiplier (0.918) was reached under the old formula but isn't demonstrably wrong —
it's simply unverifiable which of its historical batches were skewed by unmet-target windows
without the debug log to check, and re-deriving that from stored settings alone isn't possible.

Resetting every user's per-profile flow calibration to 1.0 (the v2/v3 precedent) would force
everyone back through several batches' worth of shots to reconverge, for a defect that this
change's own evidence shows is workload-dependent and often dormant (e.g., not visibly affecting
this repo's own DE1 at all). That cost isn't justified when the alternative — clear only the
*pending, not-yet-applied* accumulator — fully prevents the one concrete correctness problem
(mixing old-formula and new-formula ideals in the same median) at a fraction of the disruption:
worst case, a profile that was 1-4 shots into its next batch loses that partial progress and starts
over under the corrected formula.

**Alternative considered:** do nothing (let in-flight batches mix formulas). Rejected: a batch
straddling the release could combine e.g. 3 old-formula ideals with 2 new-formula ideals into one
median, which is a real (if bounded) correctness gap for exactly the shots closest to the release,
and clearing the accumulator is cheap (`calibration/flowCalBatch` → `"{}"`, same shape as
`clearFlowCalPendingIdeals()` already does per-profile).

### Logging

Add one `qDebug()` line at the re-routing decision point, following existing marker conventions in
this file: name the measured and target flow, the deviation, and which formula was chosen — placed
immediately after the existing `"Auto flow cal: window mode="` line so a submitted log shows the
classification and the formula choice adjacently, the way this investigation had to reconstruct by
hand from separate `meanMachineFlow` and `"using target flow"` lines.

## Risks / Trade-offs

- **[Risk]** The 10% threshold is chosen from a small sample (8 windows across one user's logs
  plus 3 on this repo's DE1) → **Mitigation**: the threshold sits far from both clusters observed
  (targets met at 99-101%, missed at 57-67%), so it isn't sensitive to being off by a few points;
  revisit only if a future report shows windows landing in that gap.
- **[Risk]** Losing in-progress batch accumulation on upgrade means a small regression in "time to
  next calibration update" for profiles mid-batch → **Mitigation**: bounded to at most 4 shots'
  worth of progress per profile, one time, and only for profiles that have an in-flight batch at
  all; the correctness gap it closes (mixed-formula medians) is worse than the delay.
- **[Risk]** A window near the 10% boundary could flip formula choice shot-to-shot on a borderline
  profile, producing a somewhat noisier ideal sequence than a hard classification would →
  **Mitigation**: this is inherent to a threshold-based reclassification, and the batch median
  already exists specifically to absorb per-shot noise; no observed evidence yet of a profile that
  sits at the boundary.

## Migration Plan

1. Add a one-time settings migration inline in the `Settings` constructor (`src/core/settings.cpp`
   — migrations run inline there, not through a separate `runMigrations()` method; corrected from
   this document's earlier assumption), immediately beside `calibration/v3FlowProfileReset`, gated
   on a new key (`calibration/v4AchievedFlowFormulaReset`), that clears `calibration/flowCalBatch`
   to `"{}"` via a new `SettingsCalibration::clearAllFlowCalPendingIdeals()` and leaves
   `calibration/perProfileFlow` and `flowCalibrationMultiplier` untouched.
2. No firmware, MCP, or profile-format changes — this is a pure app-side logic + one-time
   settings-migration change, ships in a normal release.
3. **Rollback**: reverting the code change is sufficient; the migration key prevents it from
   re-running on downgrade-then-upgrade, and clearing a pending batch has no user-visible effect
   beyond one extra shot before the next calibration update (batches were already lossy — see
   "Bean/grind changes within a batch" in `docs/CLAUDE_MD/AUTO_FLOW_CALIBRATION.md`).
