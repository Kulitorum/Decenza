# Auto Flow Calibration

## Problem

The DE1's flow sensor accuracy varies across flow rate ranges. A single global calibration multiplier works well for espresso (~1-2 ml/s) but can be significantly off for high-flow profiles like Filter 3 (~6-8 ml/s). The de1app solves this with manual per-profile calibration using a graphical tool. We can do better.

## Solution

Automatic per-profile flow calibration using scale data as ground truth. After each shot, the app compares the machine's flow sensor readings against the scale's weight-derived flow rate during the steady-pour phase, and computes the ideal calibration multiplier for that profile.

## How It Works

1. **Shot completes** with a Bluetooth scale connected
2. **Steady-state detection**: The algorithm scans the shot data for a window where:
   - Pressure is stable (change <= 0.5 bar/sec)
   - Pressure is above 1.5 bar (rejects empty-portafilter shots)
   - Weight flow is meaningful (> 0.5 g/s)
   - Machine flow is meaningful (> 0.1 ml/s)
   - Scale data is recent (nearest weight flow point within 1 second)
   - Window lasts at least 5 seconds
3. **Compute ratio**: `current_multiplier * mean(weight_flow) / mean(machine_flow)` over the steady window
4. **Density correction**: Multiply by ~0.963 (water density at ~93°C vs room temp)
5. **Sanity check**: Clamp to [0.5, 1.8] — cap extreme values that likely indicate measurement errors
6. **Store & apply**: If the computed value differs from current by > 2%, save it per-profile and send to the machine

## Algorithm Details

### Steady-State Window Detection

The algorithm iterates through the shot's pressure data looking for the longest contiguous segment where:
- Pressure is stable (change <= 0.5 bar/sec)
- Pressure is above 1.5 bar (rejects no-coffee/empty-portafilter shots where water flows freely with near-zero back-pressure)
- Weight flow > 0.5 g/s (excludes dripping/dead time)
- Machine flow > 0.1 ml/s (excludes stalled flow)
- Nearest scale data point within 1 second (ensures weight flow data alignment)

Any sample that fails these criteria breaks the current window, and the algorithm picks the longest qualifying window from the entire shot. The window must span at least 4 seconds with at least 5 samples to provide a reliable average.

### Stream Force Rejection

Before running calibration, the algorithm checks whether the settled weight dropped significantly below the weight at pump stop (> 3g drop). This indicates the stream of water hitting the cup was adding downward force to the scale during extraction, inflating the weight readings. Calibrating against these inflated readings would produce a multiplier that's too high, so the shot is skipped. This typically occurs with high-flow profiles where the stream has significant momentum.

### Density Correction

The machine flow sensor measures volumetric flow (ml/s), while the scale measures mass (g/s). Water at ~93°C has a density of ~0.963 g/ml, so the correction factor accounts for this difference. The formula is:

```
calibration = current_multiplier * mean(weight_flow) / (mean(machine_flow) * 0.963)
```

### Convergence

The multiplier is only updated when the computed value differs from the current effective multiplier by more than 2%. This prevents unnecessary writes and oscillation from measurement noise.

### Sanity Bounds

The computed multiplier is clamped to [0.5, 1.8]. Values outside this range indicate measurement errors (e.g., scale drift, splash, evaporation) rather than genuine calibration offsets.

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
- `flowCalibrationMultiplier` (double, default 1.0): Global multiplier, auto-updated to espresso median
- Effective multiplier: per-profile if auto-cal is on and one exists, otherwise falls back to global `flowCalibrationMultiplier`

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

## Limitations

- **Requires Bluetooth scale**: No scale data = no auto-calibration (silently skipped)
- **Needs steady-state flow**: Very short shots or highly variable profiles may not have a qualifying window
- **Density is approximated**: Uses a fixed 0.963 factor; actual density varies slightly with temperature
- **One multiplier per profile**: Does not calibrate different flow rate ranges within a single profile
- **Not retroactive**: Only applies to shots made after enabling the feature
