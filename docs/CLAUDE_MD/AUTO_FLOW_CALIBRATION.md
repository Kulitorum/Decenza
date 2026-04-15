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
- **Migration**: Existing users were migrated to default-on via a one-time settings migration that clears the old key

## Technical Details

### Settings Storage

- `autoFlowCalibration` (bool, default `true`): Master toggle
- `calibration/perProfileFlow` (JSON object): Maps profile filename → multiplier
- `calibration/flowCalBatch` (JSON object): Maps profile filename → array of pending ideal values (accumulator for batched updates)
- `flowCalibrationMultiplier` (double, default 1.0): Global multiplier, auto-updated to espresso median
- Effective multiplier: per-profile if auto-cal is on and one exists, otherwise falls back to global `flowCalibrationMultiplier`
- Clearing a profile's calibration (via MCP or settings UI) also clears its pending batch

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

## Limitations

- **Requires Bluetooth scale**: No scale data = no auto-calibration (silently skipped)
- **Needs steady-state flow**: Very short shots or highly variable profiles may not have a qualifying window
- **Density is approximated**: Uses a fixed 0.963 factor; actual density varies slightly with temperature
- **One multiplier per profile**: Does not calibrate different flow rate ranges within a single profile
- **Not retroactive**: Only applies to shots made after enabling the feature
- **5-shot batch delay**: First calibration update requires 5 qualifying shots on a profile. The pump runs at the global multiplier (or 1.0 on fresh install) until then.
- **Bean/grind changes within a batch**: If beans or grinder setting change within a 5-shot batch, the median blends data from different conditions. The median's outlier rejection mitigates this for small numbers of changed shots.
