# Auto Flow Calibration

## Problem

The DE1's flow sensor accuracy varies across flow rate ranges. A single global calibration multiplier works well for espresso (~1-2 ml/s) but can be significantly off for high-flow profiles like Filter 3 (~6-8 ml/s). The de1app solves this with manual per-profile calibration using a graphical tool. We can do better.

## Solution

Automatic per-profile flow calibration using scale data as ground truth. After each shot, the app compares the machine's flow sensor readings against the scale's weight-derived flow rate during the steady-pour phase, and computes the ideal calibration multiplier for that profile.

## How It Works

1. **Shot completes** with a Bluetooth scale connected
2. **Steady-state detection**: The algorithm scans the shot data for a window where:
   - Pressure is stable (3-sample smoothed change <= 0.5 bar/sec)
   - Pressure is above 1.5 bar (rejects empty-portafilter shots)
   - Weight flow is meaningful (> 0.5 g/s)
   - Machine flow is meaningful (> 0.1 ml/s)
   - Scale data is recent (nearest weight flow point within 1 second)
   - Per-sample machine/weight flow ratio is within [0.4, 2.5] (rejects scale data glitches)
   - Window lasts at least 1.5 seconds with at least 7 samples
3. **Window classification**: Checks which profile frames were actually active during the steady window (using the shot's `PhaseMarker` stream) to decide whether to use the flow or pressure formula — see "Window-level Classification" below
4. **Ratio guard**: Rejects windows where flow and weight diverge too much ([0.75, 1.35]). For flow profiles, compares `(target_flow * 0.963) / weight_flow`. For pressure profiles, compares `machine_flow / weight_flow`.
5. **Compute calibration** (formula depends on profile type):
   - **Flow profiles**: `mean(weight_flow) / (target_flow * 0.963)` — uses the profile's known target flow, independent of current calibration
   - **Pressure profiles**: `current_multiplier * mean(weight_flow) / (mean(machine_flow) * 0.963)` — divides out current calibration from reported flow
6. **Sanity check**: Clamp to `[0.5, kCalibrationMax]` — cap extreme values that likely indicate measurement errors. The upper bound tracks DE1 firmware (1.8 on pre-v1337 firmware, 2.7 on v1337+)
7. **Batch accumulate**: Add the ideal to a per-profile batch (persisted in settings). After 5 shots, compute the batch median and update C using `0.5 * median + 0.5 * current`. Only apply if the change exceeds 3%.

## Algorithm Details

### Steady-State Window Detection

The algorithm iterates through the shot's pressure data looking for the longest contiguous segment where:
- Pressure is stable (3-sample moving average change <= 0.5 bar/sec — smoothing filters PID jitter on flow profiles)
- Pressure is above 1.5 bar (rejects no-coffee/empty-portafilter shots where water flows freely with near-zero back-pressure)
- Weight flow > 0.5 g/s (excludes dripping/dead time)
- Machine flow > 0.1 ml/s (excludes stalled flow)
- Nearest scale data point within 1 second (ensures weight flow data alignment)
- Per-sample machine/weight flow ratio within [0.4, 2.5] (rejects individual scale data glitches — generous bounds since single samples are noisy)

Pressure is smoothed with a 3-sample centered moving average before computing dpdt. This filters the DE1's PID pressure corrections (~0.1-0.2 bar every ~0.2s) that would otherwise break the window on flow profiles. The original (unsmoothed) pressure is used for the minimum pressure check. Analysis of 13 D-Flow shots showed this increases average window duration from 10.7s to 16.3s.

Any sample that fails these criteria breaks the current window, and the algorithm picks the longest qualifying window from the entire shot. The window must span at least 1.5 seconds with at least 7 samples to provide a reliable average.

### Window Ratio Guard

After finding the best window, the algorithm checks whether the mean machine/weight flow ratio falls within [0.75, 1.35]. This guard rejects entire windows where scale data is systematically unreliable — for example, when weight flow smoothing lag causes the scale to consistently under-report during part of the pour.

Without this guard, shots with poor scale data quality can produce calibration values around 0.6 when the correct value is ~0.9-1.0. These bad values corrupt the global median (see below), which in turn poisons new profiles via inheritance. Analysis of 26 D-Flow shots on a properly GFC-calibrated machine showed 5 outlier shots (ratio 1.4-1.7x) that would have dragged the calibration from the correct ~0.95 down to ~0.63. The ratio guard rejects these outliers while accepting the 21 good shots.

### Stream Force Rejection

Before running calibration, the algorithm checks whether the settled weight dropped significantly below the weight at pump stop (> 3g drop). This indicates the stream of water hitting the cup was adding downward force to the scale during extraction, inflating the weight readings. Calibrating against these inflated readings would produce a multiplier that's too high, so the shot is skipped. This typically occurs with high-flow profiles where the stream has significant momentum.

### Window-level Classification

The calibration formula depends on whether the DE1 was holding flow or pressure **during the steady window** — not on the profile as a whole. The classifier lives in `src/controllers/autoflowcalclassifier.cpp` and inspects the `PhaseMarker` stream recorded during the shot to find every frame touched by `[windowStart, windowEnd]`, then:

- If every touched frame is flow-controlled (`pump == "flow"`, flow > 0.1) → use the **flow** formula. The target is the flow of the touched frame closest to the observed mean machine flow (handles multi-target profiles).
- If every touched frame is pressure-controlled → use the **pressure** formula.
- If touched frames are mixed (window straddles a flow↔pressure transition) → skip; logged as `"window spans mixed flow/pressure frames — skipping (ambiguous target)"`.
- If phase-marker data is missing (e.g. legacy shot or very short capture) → fall back to the historical profile-level scan so calibration still runs.

**Why window-level matters.** Hybrid profiles like ASL9-3 have both pressure decline frames and a flow-controlled tail. A profile-level scan classifies the whole profile as "flow" because any flow frame is present, even when every observed steady window lands in the pressure declines. Under v3's flow formula this produced false `"flow profile ratio … outside bounds"` rejections and occasional 20%+ single-shot jumps when a short window slipped through. Window-level classification routes those same windows to the pressure branch instead — see issue #739 for the full analysis.

**Off-target windows are skipped (v5).** A flow-controlled frame can still carry a pressure ceiling — D-Flow and D-Flow/Q both pour flow-controlled with a pressure limit (`limiter.value` in the frame JSON). When the puck's resistance would need more pressure than that ceiling to hold the frame's target flow, the DE1 caps pressure instead and flow falls below target for the rest of the frame. Before computing an ideal, the algorithm compares the window's measured `meanMachineFlow` against the touched frame's target flow (`autoFlowCalWindowTargetCheck()` in `autoflowcalclassifier.{h,cpp}`); an **undershoot** over 10% skips the window entirely — that shot contributes no ideal.

**Why skip rather than measure it another way.** The window is a real measurement, but of a flow rate this profile does not pour at, and one multiplier cannot describe two operating points. Measured on one DE1 at a *fixed* multiplier, the ratio of scale weight flow to reported machine flow rises monotonically as flow falls:

| mean machine flow | weight flow / machine flow |
|---|---|
| 1.90 ml/s | 0.76 |
| 1.79 | 0.87 |
| 1.10 | 1.03 |
| 0.72 | 1.13 |

A 48% swing with no calibration change. So a capped window and a target-met window on the same profile produce ideals 30-40% apart, and averaging them is what makes a stored multiplier wander.

Between 2.0.4 and v5 such a window was instead **re-routed** through the achieved-flow (pressure-branch) formula. That did not remove the disagreement, it reversed its sign: the [#1872](https://github.com/Kulitorum/Decenza/issues/1872) reporter's capped window produced 0.902 through the flow branch (dragging his multiplier down — the original [#1823](https://github.com/Kulitorum/Decenza/issues/1823) complaint) and 1.351 through the achieved-flow branch (pushing it up), so his value oscillated with how many of the week's shots capped. Do not reinstate the re-route without new evidence; the replay across three machines is in `openspec/changes/skip-off-target-flow-cal-windows/`.

A consequence worth stating: a profile that *always* caps accumulates no ideals and its multiplier never moves. That is the honest outcome — there is no on-target data to learn from — and each skip is logged, so it is diagnosable from a submitted debug log rather than silent.

The check is deliberately one-sided: only undershoot skips, never overshoot. A pressure ceiling can hold flow below its setpoint but has no mechanism to push flow above one, so an overshoot reading has no pressure-cap explanation; the window is still genuinely flow-controlled and keeps the target-flow formula, which is what protects flow windows from the v2 feedback-loop bug below. Real-world numbers from #1823: target-achieved windows measured 99-101% of target flow; target-missed (pressure-capped) windows measured 57-67%, well clear of the 10% threshold on both sides.

**Flow profiles** (e.g., D-Flow, Filter) or **flow-controlled windows**: The DE1's PID servo holds the reported flow at the target flow regardless of the calibration factor. Using the reported flow in the formula creates a feedback loop: lowering the factor → less pumping → lower weight flow → factor keeps drifting down, never converging. Instead, the formula uses the target flow directly:

```
calibration = mean(weight_flow) / (target_flow * 0.963)
```

This is independent of the current calibration factor and converges correctly.

**Pressure profiles** (e.g., Classic Italian) or **pressure-controlled windows**: The machine controls pressure, not flow, so the reported flow reflects actual sensor readings (already multiplied by the calibration factor). The formula divides out the current factor to recover raw sensor flow:

```
calibration = current_multiplier * mean(weight_flow) / (mean(machine_flow) * 0.963)
```

### Density Correction

The machine flow sensor measures volumetric flow (ml/s), while the scale measures mass (g/s). Water at ~93°C has a density of ~0.963 g/ml, so the correction factor accounts for this difference.

### Batched Median Updates

Instead of updating the calibration factor after every shot (which changes pump behavior and creates a feedback loop), the algorithm accumulates ideal values across 5 shots at a constant calibration, then updates once using the batch median.

**Why batching?** Each calibration update changes the pump's flow setpoint, which changes puck extraction dynamics. Two identical pucks pulled at different C values produce different weight flows. Per-shot updates cause the algorithm to partially chase its own tail — each update changes the conditions for the next shot. Batching ensures 5 shots are pulled under identical pump conditions, producing truly comparable data.

**Why median?** The median provides natural outlier rejection. Runaway shots, channeling anomalies, and other one-off events are automatically ignored without needing explicit detection logic.

**Update rule:**
- Accumulate 5 ideals per profile (persisted in settings across app restarts)
- Compute median of the batch
- Blend: `new_C = 0.5 * median + 0.5 * current_C` (alpha=0.5 is safe because the median of 5 shots is more reliable than a single ideal)
- First calibration for a profile uses the median directly (no history to blend with)
- Only apply if the change exceeds 3% — prevents unnecessary pump changes when the factor is already close

After the update, the batch resets and accumulation begins again at the new C value. The algorithm continues monitoring indefinitely but only changes C when the shift is meaningful.

### Sanity Bounds

The computed multiplier is clamped to `[0.5, kCalibrationMax]`, where `kCalibrationMax` is firmware-dependent:

- **Pre-v1337 firmware** (classic pumps): 1.8 — matches the historical upper bound; values this high on older pumps almost always indicate measurement errors (scale drift, splash, evaporation) rather than genuine offsets.
- **v1337+ firmware** (newer pump hardware): 2.7 — the firmware-side cap was raised from 2.0 to 3.0, and auto-cal keeps ~10% headroom below that (same 0.9× ratio the old pair used).

Values above the classic 1.8 ceiling (but under the new 2.7 one) are legitimate on v1337+ firmware but are logged via `qWarning` and surfaced in the Profile Info page with an amber color and an "unusually high — verify scale accuracy" screen-reader description, so the user can sanity-check their scale before trusting the new value.

Per-profile persistence (`Settings::setProfileFlowCalibration`) and the settings importer (`SettingsSerializer`) both accept `[0.5, 2.7]` regardless of the currently-connected firmware — the runtime compute-time gate is what enforces the stricter 1.8 cap on older firmware, so stored values from a v1337+ session remain readable if the user later downgrades firmware. The window-ratio guard `[0.75, 1.35]` (see above) remains the primary protection against bad scale data across both firmware ranges.

## User Experience

- **Default ON**: Auto calibration is enabled by default for all users
- **Disable toggle**: Settings > Preferences > Flow Calibration > "Disable auto calibration"
- **Automatic operation**: Calibration happens silently after each qualifying shot
- **Toast notification**: Brief notification when a calibration update occurs (e.g., "Flow cal updated for Filter 3: 1.00 → 1.08")
- **Profile Info**: Shows the effective multiplier with "(global)" or "(auto)" label
- **Manual override disabled**: When auto-cal is on, the Calibrate button is greyed out
- **Setting a known value by hand (MCP)**: `set_flow_calibration` writes the per-profile multiplier directly, for a user who already knows the right number for a profile. It takes effect immediately, but it is a *starting point*, not a pin: with auto-cal on, later batches keep moving it. Pinning an exact number still means turning auto-cal off and setting the **global** multiplier — with auto-cal off, per-profile values are ignored entirely (`effectiveFlowCalibration`), so a per-profile write alone is stored but inert. The tool reports that case in a `warning` field rather than silently doing nothing. `get_flow_calibration` describes the resulting state (stored vs. effective value, which source is in use, batch progress).
- **Migration**: Existing users were migrated to default-on via a one-time settings migration that clears the old key

## Technical Details

### Settings Storage

- `autoFlowCalibration` (bool, default `true`): Master toggle
- `calibration/perProfileFlow` (JSON object): Maps profile filename → multiplier
- `calibration/flowCalBatch` (JSON object): Maps profile filename → array of pending ideal values (accumulator for batched updates)
- `flowCalibrationMultiplier` (double, default 1.0): Global multiplier, auto-updated to espresso median
- Effective multiplier: per-profile if auto-cal is on and one exists, otherwise falls back to global `flowCalibrationMultiplier`
- Clearing a profile's calibration (via MCP or settings UI) also clears its pending batch. So does *setting* one: `setProfileFlowCalibration()` clears the pending ideals itself, because they were computed against the old C and would otherwise be folded into the next batch median at the new one. The auto-cal path already cleared them a few lines earlier, so there it is a no-op; the manual (MCP) and settings-import writers are the ones that need it.

### Profile Load Hook

When a profile is loaded (user switch or startup), `applyFlowCalibration()` is called. If auto-cal is on and a per-profile multiplier exists for the loaded profile, that value is sent to the machine. Otherwise the global multiplier is used.

### Global from Espresso Median

After each per-profile calibration update, the global multiplier is updated to the median of all espresso per-profile values. This helps new profiles converge faster — instead of starting at 1.0, they start near the machine's actual calibration.

- Requires at least 2 espresso profiles with per-profile calibrations
- Uses IQR fence method (1.5× IQR from Q1/Q3) to remove outliers when 4+ profiles exist
- Falls back to all values if outlier filtering leaves fewer than 2
- Only updates the global if the median differs from current by more than 2%
- Non-espresso profiles (e.g., filter) are excluded from the median since they operate at very different flow rates

### MMR Write

The calibration multiplier is written to the DE1 via the existing `DE1Device::setFlowCalibrationMultiplier()` method, which writes to the appropriate MMR address.

## v2 Migration (Ratio Guard Reset)

A one-time migration resets all per-profile flow calibrations and the global multiplier to 1.0. This is necessary because the pre-v2 algorithm had no ratio guards, allowing shots with poor scale data to drag calibrations down to ~0.6. The corrupted values then spread via the global median to new profiles (bootstrap problem). After the reset, the improved algorithm re-converges to correct values (~0.9-1.0) within a few shots per profile.

## v3 Migration (Flow Profile Feedback Loop Fix)

A one-time migration resets all per-profile flow calibrations and the global multiplier to 1.0. The v2 algorithm had a feedback loop for flow-controlled profiles: the DE1's PID holds reported flow at the target regardless of calibration, so the formula `ideal = factor * weightFlow / (reportedFlow * density)` made the ideal proportional to the current factor — it could only decrease, never converge. Over 30 shots a user's factor drifted from 1.0 to 0.59. The v3 algorithm uses the profile's target flow directly for flow profiles, breaking the loop. The reset clears all calibrations (including pressure profiles) since the global median may have been contaminated by drifted flow-profile values.

## v4 Migration (Pressure-Capped Flow Window Fix)

A one-time migration clears every profile's *pending* flow-cal batch only (`calibration/flowCalBatch` → `{}`) — unlike v2 and v3, it does **not** reset stored per-profile or global multipliers. The v3 formula assumed a flow-controlled frame always achieves its target flow; on a pressure-capped flow frame it often doesn't (see "Achieved-flow deviation check" above), producing a bad ideal for that specific window. That's a per-window defect the batch median's outlier rejection already partially absorbs, not evidence the *stored* multiplier is wrong the way v2/v3's corrupted values were — so a full reset would cost every user several shots' worth of reconvergence to fix a defect that's workload-dependent and often dormant (profiles that reliably hit target flow were never affected). Clearing only the pending accumulator is enough to stop a batch from mixing ideals computed under the old and new formula-selection logic in one median.

## Flow Calibration Recorded Per Shot

Each saved shot stores the effective multiplier it POURED under, in `shots.flow_calibration` (migration 39). Without it a shot's `flow` curve is uninterpretable on its own — reported flow is a calibrated quantity, so comparing two shots or recovering a raw sensor reading needs the multiplier that produced them, and diagnosing [#1872](https://github.com/Kulitorum/Decenza/issues/1872) needed a debug log beside the shot data for exactly that reason.

The value is latched at shot START (`ProfileManager::latchForShot()`), not read at save time: `computeAutoFlowCalibration()` runs at shot end BEFORE the save and can write a new per-profile multiplier first, so a save-time read would record the value the shot PRODUCED on exactly the shots where it changed.

The latch is **consumed** by the save, so a value belongs to exactly the shot that latched it: a shot whose `espressoCycleStarted` transition was missed reads 0 and records NULL rather than inheriting its predecessor's multiplier.

NULL means **not recorded** — a shot saved before the column existed, an imported shot whose source lacked it, the dev fake-shot path, or a shot that never latched. It is never read as 1.0, which is a legitimate multiplier; the projection omits the field entirely rather than emitting a sentinel.

## v5 Migration (Off-Target Windows Skipped)

Same shape as v4 and for the same reason: which windows produce an ideal changed, so a batch must not mix the two rules in one median. A one-time migration (`calibration/v5SkipOffTargetReset`) clears every profile's *pending* batch only. Stored per-profile and global multipliers are deliberately left alone — the defect is per-window, and auto-calibration walks the value back within a few batches once capped windows stop contributing. Resetting would cost every well-dialled user several batches of reconvergence for a defect their data does not show: replayed over 30 D-Flow shots from a well-dialled machine, contribution drops 30/30 → 27/30 and the median ideal does not move at all (0.968 → 0.968).

## Limitations

- **Requires Bluetooth scale**: No scale data = no auto-calibration (silently skipped)
- **Needs steady-state flow**: Very short shots or highly variable profiles may not have a qualifying window
- **Density is approximated**: Uses a fixed 0.963 factor; actual density varies slightly with temperature
- **One multiplier per profile**: Does not calibrate different flow rate ranges within a single profile — and the sensor's error genuinely varies with flow rate (see the table under "Window-level Classification"), which is why windows that miss their frame's target flow are skipped rather than averaged in. A profile whose pours consistently cap will never calibrate.
- **Not retroactive**: Only applies to shots made after enabling the feature
- **5-shot batch delay**: First calibration update requires 5 qualifying shots on a profile. The pump runs at the global multiplier (or 1.0 on fresh install) until then.
- **Bean/grind changes within a batch**: If beans or grinder setting change within a 5-shot batch, the median blends data from different conditions. The median's outlier rejection mitigates this for small numbers of changed shots.
