# Shot Timer & Weight System Refactor - COMPLETED

## Summary

Created `ShotTimingController` to centralize all shot timing, tare management, and weight processing. This eliminates the previous architecture where timing was spread across MachineState, MainController, and ShotDataModel with three independent timestamp sources.

## What Was Fixed

1. **Shot timer showed wrong values** - Now uses DE1's BLE timer as single source of truth
2. **Weight graph showed flow rate (~6 g/s) instead of cumulative weight** - Fixed to show 0g → 36g progression
3. **Stop-at-weight failed or overshot significantly** - Now uses flow-rate-based lag compensation
4. **Tare race conditions** - Fire-and-forget tare with extraction-start gating
5. **Steam/HotWater timer used espresso time** - Fixed to use independent local timer for non-espresso operations

## Architecture

### New: ShotTimingController

Single source of truth for espresso shot timing:

```
src/controllers/shottimingcontroller.h
src/controllers/shottimingcontroller.cpp
```

**Responsibilities:**
- Shot timing using DE1's BLE timer (`sample.timer`)
- Tare state management (fire-and-forget with extraction-start gating)
- Weight-to-timestamp synchronization
- Stop-at-weight detection with lag compensation
- Per-frame weight exit detection

**Key Properties:**
- `shotTime` - Current shot time (from BLE timer)
- `tareComplete` - Always true after tare() called (fire-and-forget)
- `currentWeight` - Current scale weight

**Key Signals:**
- `sampleReady(time, pressure, flow, temp, ...)` - Unified sample for graph
- `weightSampleReady(time, weight)` - Weight sample with synchronized timestamp
- `stopAtWeightReached()` - Triggers machine stop
- `perFrameWeightReached(frameNumber)` - Triggers frame skip

### Signal Flow

```
DE1Device (BLE samples)
    │
    └─► MainController.onShotSampleReceived()
            └─► ShotTimingController.onShotSample()
                    └─► emit sampleReady() ─► ShotDataModel.addSample()

ScaleDevice (weight samples)
    │
    └─► MainController.onScaleWeightChanged()
            └─► ShotTimingController.onWeightSample()
                    └─► emit weightSampleReady() ─► ShotDataModel.addWeightSample()
                    └─► checkStopAtWeight() ─► emit stopAtWeightReached()
                    └─► checkPerFrameWeight() ─► emit perFrameWeightReached()
```

### MachineState Changes

`shotTime()` now delegates to ShotTimingController for espresso phases only:

```cpp
double MachineState::shotTime() const {
    bool isEspressoPhase = (m_phase == Phase::EspressoPreheating ||
                           m_phase == Phase::Preinfusion ||
                           m_phase == Phase::Pouring ||
                           m_phase == Phase::Ending);
    if (m_timingController && isEspressoPhase) {
        return m_timingController->shotTime();
    }
    // Use local timer for steam/hot water/flush
    return m_shotTime;
}
```

## Key Implementation Details

### Tare Handling (Fire-and-Forget)

```cpp
void ShotTimingController::tare() {
    if (m_scale && m_scale->isConnected()) {
        m_scale->tare();
        m_scale->resetFlowCalculation();
    }
    // Assume tare worked immediately
    m_weight = 0;
    m_tareState = TareState::Complete;
}
```

Weight samples are ignored until extraction starts (frame 0 reached), giving the scale 4-5 seconds of preheating time to process the tare command.

### Extraction Start Detection

During preheating, DE1 reports high frame numbers (e.g., 2 or 3). When extraction starts, frame becomes 0:

```cpp
if (frameNumber == 0 && !m_extractionStarted) {
    m_extractionStarted = true;
    // Now weight samples will be processed
}
```

### Stop-at-Weight Lag Compensation

```cpp
double lagSeconds = (state == DE1::State::HotWater) ? 1.2 : 1.5;
double lagCompensation = flowRate * lagSeconds;

if (m_weight >= (target - lagCompensation)) {
    emit stopAtWeightReached();
}
```

With real coffee: Target 36g → triggered at 33.9g → final 36.9g (0.9g over)

### Weight Graph Fix

ShotDataModel now plots cumulative weight, not flow rate:

```cpp
void ShotDataModel::addWeightSample(double time, double weight) {
    if (weight < 0.1) return;  // Ignore noise

    if (m_weightPoints.isEmpty()) {
        m_weightPoints.append(QPointF(time, 0.0));  // Start from zero
    }
    m_weightPoints.append(QPointF(time, weight));  // Plot WEIGHT, not flowRate
}
```

## Files Modified

### New Files:
- `src/controllers/shottimingcontroller.h`
- `src/controllers/shottimingcontroller.cpp`

### Modified:
- `src/main.cpp` - Create and wire ShotTimingController
- `src/machine/machinestate.cpp` - Delegate shotTime() for espresso phases
- `src/machine/machinestate.h` - Add timing controller pointer
- `src/controllers/maincontroller.cpp` - Forward samples to timing controller
- `src/controllers/maincontroller.h` - Add timing controller pointer
- `src/models/shotdatamodel.cpp` - Plot weight instead of flowRate
- `src/models/shotdatamodel.h` - Add overload for weight-only samples
- `src/ble/de1device.cpp` - Add [REFACTOR] logging
- `CMakeLists.txt` - Add new source files

## Debug Logging

All timing/weight flow has `[REFACTOR]` tagged logging for troubleshooting:

```
[REFACTOR] ShotTimingController::startShot()
[REFACTOR] TARE SENT (fire-and-forget)
[REFACTOR] EXTRACTION STARTED - frame 0 reached, weight tracking enabled
[REFACTOR] WEIGHT->GRAPH: time=X.XX weight=X.XX frame=N
[REFACTOR] STOP-AT-WEIGHT TRIGGERED: weight=X target=Y lagComp=Z
[REFACTOR] ShotTimingController::endShot()
```

Filter logs with: `adb logcat | grep "\[REFACTOR\]"`

## Verification Results

- [x] Shot timer shows 0 during preheating
- [x] Shot timer starts from 0 at extraction (frame 0)
- [x] Shot timer runs smoothly (no jumps)
- [x] Shot timer stops when shot ends
- [x] Weight graph shows cumulative weight (0g → 36g)
- [x] Weight graph timestamps align with pressure/flow
- [x] Stop-at-weight triggers correctly with lag compensation
- [x] Per-frame weight exit works (e.g., 0.5g exit on Fill frame)
- [x] Works with physical scale (Skale)
- [x] Works with FlowScale fallback
- [x] Steam/HotWater/Flush use independent timers
- [x] Multiple consecutive shots work correctly
- [x] Stale weight ignored during preheating
