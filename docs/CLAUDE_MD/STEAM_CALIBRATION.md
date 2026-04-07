# Steam Calibration

## Status: Experimental (Hidden)

The steam calibration UI is currently hidden from users. The infrastructure (data collection, stability analysis, MCP tools) works well, but the **recommendation algorithm is not reliable enough** to ship. The community-established defaults (160°C / 0.8 mL/s for Pro/XL, ~1.0-1.2 for XXL) consistently outperform the tool's recommendations. See "Lessons Learned" below.

## Overview

The DE1 uses a flash heater (not a boiler) to generate steam on demand. Different models have different heater wattages (Pro/XL: 1.5kW, XXL: 2.2kW, Bengle: 3kW), and voltage (110V vs 220V) further affects actual heating power. Finding optimal steam settings is model- and setup-dependent.

## The Physics

### Flash Heater Sweet Spot

The optimal steam flow rate is the **highest rate where the heater can fully vaporize all water passing through it**:

- **Too low**: The heater overheats and triggers a protective flow increase, creating an oscillating/sawtooth pressure curve. The steam is dry but the vortex is weak.
- **Too high**: More water passes through than the heater can vaporize, producing wet steam (more water added to milk). The vortex is strong but microfoam quality suffers.
- **Sweet spot**: Stable pressure curve, maximum steam dryness, and enough kinetic energy for a strong vortex.

### Approximate Sweet Spots by Model

| Model | Heater | Approx. Sweet Spot |
|-------|--------|-------------------|
| DE1 Pro/XL | 1.5kW | ~0.6-0.8 mL/s |
| DE1 XXL | 2.2kW | ~0.9-1.2 mL/s |
| Bengle | 3.0kW | ~1.2-1.5 mL/s |

These vary by voltage (110V shifts the sweet spot down) and individual machine calibration.

### Dilution Math

To raise 180g of milk by 60 degrees C, the theoretical minimum water addition from steam condensation is ~11.3% (accounting for pitcher thermal mass). In practice, expect 12-15% due to heat losses. The 1-2% difference between machines is negligible in the cup (milk is already ~88% water), but steam speed and kinetic energy noticeably affect microfoam texture and latte art quality.

### Flow Calibration Affects Steam

The Graphical Flow Calibrator (GFC) multiplier affects the machine's internal flow measurement during steaming, not just espresso. A miscalibrated flow multiplier is a common hidden cause of bad steam performance.

## What Exists in the Code

### SteamCalibrator (`src/machine/steamcalibrator.h/.cpp`)

- Flow rate sweep with auto-stop at ~22 seconds per step
- Pressure stability analysis: CV, oscillation rate, peak-to-peak range, slope
- Heater exhaustion detection and trimming (pressure drops below 0.3 bar)
- Negative/discontinuous timestamp filtering
- Steam dryness estimation from heater power and flow rate
- Milk dilution estimation from thermodynamic energy balance
- Per-model heater wattage lookup with voltage adjustment
- Heater recovery detection between steps (waits for steam temp within 5°C of target)
- Temporarily enables `keepSteamHeaterOn` during calibration
- Sends updated settings to machine when advancing steps
- Raw time-series data logging to JSON file
- QSettings persistence of calibration results

### QML Dialog (`qml/components/SteamCalibrationDialog.qml`)

- Guided wizard with step indicator and countdown timer
- Heater recovery status with live temperature display
- Results table showing flow, pressure, CV, and stability bars
- Beta notice banner
- Currently not accessible from the UI (button commented out in SettingsCalibrationTab.qml)

### MCP Tools (`src/mcp/mcptools_machine.cpp`)

- `steam_calibration_status`: Returns summary results including recommended flow, CV, dilution
- `steam_calibration_log`: Returns full raw time-series data for post-run analysis

### Machine Info (`src/ble/de1device.h/.cpp`)

- `machineModel` property exposed (was private)
- `heaterVoltage` read from MMR 0x803834 (new)

## Lessons Learned from Testing

### 1. Steaming into air vs. water produces similar pressure patterns
Air and water tests on a DE1+ 120V showed comparable CV values and pressure curves. Steaming into air is simpler (no pitcher, no water changes) and sufficient for measuring heater behavior.

### 2. The heater exhausts after 30-50 seconds
On a DE1+ 120V, pressure drops to near zero after 30-50 seconds as the flash heater's thermal mass is depleted. This corrupts stability metrics if not trimmed. The auto-stop at 22 seconds prevents this, and the exhaustion detection trims any tail that gets through.

### 3. Heater recovery requires `keepSteamHeaterOn`
The DE1 firmware only maintains steam temperature in Ready state when `keepSteamHeaterOn` is true. Without it, the heater cools between steps and never reaches the target. The calibrator temporarily enables this setting.

### 4. The DE1 firmware heats the steam element during the Heating phase on wake, but does not actively re-heat after a steaming session depletes it
After a steam session ends and the machine returns to Ready, the steam heater temperature slowly drifts. It takes time to recover, and the recovery rate depends on `keepSteamHeaterOn`.

### 5. Temperature sweep (Phase 2) is not useful when steaming into air
Changing the temperature setting while steaming into air doesn't meaningfully change what we're measuring. Temperature affects milk heating and dilution, but without milk the pressure stability is primarily a function of flow rate vs. heater capacity.

### 6. CV (coefficient of variation) is the right primary metric
On real DE1+ data, CV consistently identifies the sweet spot (0.80 mL/s) with a clear U-shaped curve. Absolute CV values vary by machine (DE1+ 120V: 0.19-0.30) but the relative pattern is consistent.

### 7. The recommendation algorithm is the unsolved problem
Multiple approaches were tried:
- **Stability score (weighted CV + oscillation + range + slope)**: Scored 0 on every real step because the weights were tuned for synthetic data. Real machines have much higher baseline variation.
- **Lowest dilution among stable steps**: Always picked the lowest flow rate (best theoretical dryness but weakest steam, useless in practice).
- **Highest stable flow**: Picked 1.20 mL/s on a DE1+ — too high, terrible steam quality.
- **Highest flow within 10% CV of best**: Still picked 1.20 because its CV was within the margin, ignoring that the estimated dilution was 25% vs 18% at the best CV.
- **CV + dilution cap**: Better but the theoretical dilution estimates don't reflect real-world experience. The tool recommended 0.80 in some runs, 1.20 in others.

The core challenge: **the recommendation needs to balance pressure stability, steam dryness, vortex strength, and steaming speed** — and the relative importance of each depends on the user's use case (home latte art vs. commercial volume). The community's human-tuned defaults already embody years of this balancing.

### 8. Potential future approach
Rather than making automated recommendations, the tool could:
- **Show the data**: Display the CV curve and let users see their machine's sweet spot
- **Compare to community baselines**: Show how their results compare to known-good settings for their model
- **Detect problems**: Flag if all CVs are unusually high (possible flow calibration issue) or if the heater exhausts too quickly (possible scale buildup)

## Community Research Sources

- **Eduardo Passoa's enthalpy vs. kinetic energy analysis** (Basecamp, Decent Diaspora, April 2026): Systematic comparison of DE1 XXL vs. commercial Fiamma boiler. Discovered 0.9 mL/s sweet spot for XXL by analyzing pressure curve stability. 54-comment thread with input from Decent staff.
- **Michael Garcia's steam performance testing** (Basecamp, July 2020): Systematic sweep of flow/temp combinations on 120V Pro. Settled on 165°C @ 0.6 mL/s for smoothest pressure.
- **Sergey Shevtchenko's flow calibration discovery** (Basecamp, July 2024 - September 2025): Diagnosed that a wrong GFC multiplier (1.41 instead of ~0.7) caused bad steam on his XXL. The GFC setting affects steam, not just espresso.
- **Collin Arneson's engineering analysis**: Explained that heater efficiency improves at higher pressures/flow rates, with the sweet spot where dilution starts to drop before increasing sharply at excessive flow.
- **Shinguk Kwon's Bengle confirmation**: Bengle's 3kW heater produces faster/stronger steam with a notably stronger whirlpool effect.
- **Damian's thermodynamic calculations**: ~11.3% dilution is the theoretical minimum for standard steaming parameters.

## Real Test Data (DE1+ 120V)

### Air test — CV by flow rate (representative run)
```
Flow (mL/s) | Avg Pressure | CV    | Est. Dryness | Est. Dilution
0.40        | 0.95 bar     | 0.286 | 1.00         | 10.4%
0.60        | 1.67 bar     | 0.294 | 0.74         | 13.8%
0.80        | 1.58 bar     | 0.193 | 0.55         | 17.8%    <-- best CV
1.00        | 2.11 bar     | 0.233 | 0.44         | 21.7%
1.20        | 2.19 bar     | 0.220 | 0.37         | 25.4%
```

The CV valley at 0.80 mL/s matches the community recommendation for this model.
