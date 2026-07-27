# Simple Profile Editor - Design Document

## Current Situation

### Profile Types in DE1 Ecosystem

The DE1 app supports three profile types:

| Type | Code | Description |
|------|------|-------------|
| Simple Pressure | `settings_2a` | 3-step pressure profile with time-based phases |
| Simple Flow | `settings_2b` | 3-step flow profile with time-based phases |
| Advanced | `settings_2c` | Frame-by-frame control (what we call "Advanced Editor") |

### How Simple Profiles Work

Simple profiles use **individual parameters** rather than frame arrays:

```tcl
# Simple Pressure Profile Example (Best overall pressure profile.tcl)
settings_profile_type settings_2a
advanced_shot {}                    # Empty! Frames generated from params below

# Preinfusion
preinfusion_time 20                 # Max duration (seconds)
preinfusion_flow_rate 8.0           # Flow during preinfusion (mL/s)
preinfusion_stop_pressure 4         # Exit when pressure reaches this (bar)

# Rise and Hold
espresso_hold_time 10               # Hold duration (seconds)
espresso_pressure 8.4               # Target pressure (bar)
maximum_flow 3.5                    # Flow limiter (mL/s)

# Decline
espresso_decline_time 30            # Decline duration (seconds)
pressure_end 6.0                    # End pressure (bar)

# Global
espresso_temperature 88.0
final_desired_shot_weight 36
```

### Current Implementation (January 2025)

We now correctly import simple profiles by:
1. Detecting `settings_2a` or `settings_2b` profile type
2. Generating frames from the simple parameters (matching de1app's `pressure_to_advanced_list()` / `flow_to_advanced_list()`)
3. Keeping them as **frame-based** profiles (shown in Advanced Editor)

This preserves exact timing and behavior, but users see the frame-by-frame view instead of the friendly 3-step view.

### Why D-Flow Editor Doesn't Work

Our D-Flow editor uses a different model:

| Aspect | D-Flow | Simple Profile |
|--------|--------|----------------|
| Pour termination | Weight-based (stop at X grams) | Time-based (hold for X seconds) |
| Phases | Fill → Infuse → Pour → Decline | Preinfuse → Rise & Hold → Decline |
| Hold time | Not explicit (runs until weight) | Explicit parameter (e.g., 10 seconds) |
| Decline time | Optional, weight can still stop it | Explicit parameter (e.g., 30 seconds) |

Converting to D-Flow loses the time-based behavior and produces different profiles.

---

## Proposed: Simple Profile Editor

A dedicated editor for `settings_2a` and `settings_2b` profiles that matches de1app's UI model.

### UI Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│  Simple Pressure Editor                    [Switch to Advanced]     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Profile Graph                             │   │
│  │     (pressure line, flow line, temperature line)             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │
│  │ 1: Preinfuse │  │ 2: Rise/Hold │  │ 3: Decline   │              │
│  │              │  │              │  │              │              │
│  │ Flow: 8.0    │  │ Press: 8.4   │  │ End: 6.0 bar │              │
│  │ Exit: <4 bar │  │ Time: 10s    │  │ Time: 30s    │              │
│  │ Max: 20s     │  │ Limit: 3.5   │  │              │              │
│  │ Temp: 88°C   │  │ Temp: 88°C   │  │ Temp: 88°C   │              │
│  └──────────────┘  └──────────────┘  └──────────────┘              │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ Stop at Weight: [  36.0  ] g          Dose: [ 18.0 ] g      │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  ← Back              "Best overall pressure"    4 frames   Done →  │
└─────────────────────────────────────────────────────────────────────┘
```

### Data Model: SimpleProfileParams

```cpp
struct SimpleProfileParams {
    // Profile type
    bool isPressureProfile = true;  // false = flow profile

    // Preinfusion phase
    double preinfusionTime = 20.0;          // Max duration (seconds)
    double preinfusionFlowRate = 8.0;       // Flow (mL/s)
    double preinfusionStopPressure = 4.0;   // Exit pressure (bar)
    double preinfusionTemperature = 88.0;   // Temperature (°C)

    // Rise and Hold phase (pressure profile)
    double holdTime = 10.0;                 // Duration (seconds)
    double espressoPressure = 8.4;          // Target pressure (bar)
    double maximumFlow = 3.5;               // Flow limiter (mL/s)
    double holdTemperature = 88.0;          // Temperature (°C)

    // Rise and Hold phase (flow profile) - alternative params
    double flowProfileHold = 4.0;           // Target flow (mL/s)
    double maximumPressure = 0.0;           // Pressure limiter (bar)

    // Decline phase
    double declineTime = 30.0;              // Duration (seconds)
    double pressureEnd = 6.0;               // End pressure (bar)
    double flowProfileDecline = 1.0;        // End flow for flow profiles
    double declineTemperature = 88.0;       // Temperature (°C)

    // Global
    double targetWeight = 36.0;             // Stop at weight (grams)

    // Temperature stepping (optional)
    bool temperatureStepsEnabled = false;
    double temperature0 = 88.0;  // Preinfusion temp boost
    double temperature1 = 88.0;  // Preinfusion temp
    double temperature2 = 88.0;  // Hold temp
    double temperature3 = 88.0;  // Decline temp
};
```

### Frame Generation

Reuse existing `generatePressureProfileFrames()` and `generateFlowProfileFrames()` functions, which already match de1app's logic.

### Implementation Steps

1. **Add SimpleProfileParams struct** to `src/profile/`
2. **Add detection** in Profile class: `isSimplePressureProfile()`, `isSimpleFlowProfile()`
3. **Create SimpleProfileEditor.qml** with 3-step UI
4. **Add navigation logic** in main.qml to route to correct editor based on profile type
5. **Add conversion methods**:
   - `extractSimpleParams()` - extract params from frames
   - `regenerateFromSimpleParams()` - regenerate frames from params

### Detection Logic

```cpp
bool Profile::isSimplePressureProfile() const {
    return m_profileType == "settings_2a" ||
           (m_profileType.isEmpty() && looksLikeSimplePressure());
}

bool Profile::isSimpleFlowProfile() const {
    return m_profileType == "settings_2b" ||
           (m_profileType.isEmpty() && looksLikeSimpleFlow());
}

bool Profile::looksLikeSimplePressure() const {
    // Heuristic: 3-4 frames, preinfusion + hold + decline pattern
    // All pressure-controlled (except preinfusion which is flow)
    if (m_steps.size() < 2 || m_steps.size() > 5) return false;
    // ... pattern matching
}
```

### Editor Routing

```qml
// In main.qml or profile navigation
function openProfileEditor() {
    // This used to read `MainController.currentProfilePtr` and call methods on the returned
    // Profile. That never worked from QML: the property lived on ProfileManager, not
    // MainController, and Profile is a plain C++ class with no Q_OBJECT and no Q_GADGET, so QML
    // could not reach a single member through the pointer. The property has since been removed
    // outright. Route on ProfileManager's own string instead — it is a real Q_PROPERTY.
    switch (ProfileManager.currentEditorType) {
    case "pressure":
    case "flow":
        pageStack.push(simpleProfileEditorPage)
        break
    case "dflow":
    case "aflow":
        pageStack.push(recipeEditorPage)
        break
    default:
        pageStack.push(profileEditorPage)  // Advanced
    }
}
```

### Migration Path

Users with existing imported simple profiles can:
1. Continue using Advanced Editor (frames are correct)
2. Once Simple Editor exists, profiles auto-route to it
3. No re-import needed - detection is based on frame pattern

---

## Files Changed (Current Implementation)

- `src/profile/profile.cpp` - Added `generatePressureProfileFrames()`, `generateFlowProfileFrames()`, simple profile detection in `loadFromTclFile()`
- `src/profile/profileimporter.cpp` - Added `forceImportProfile()`, `updateAllDifferent()`
- `src/profile/profileimporter.h` - Method declarations
- `qml/pages/ProfileImportPage.qml` - Added "Update All" button, long-press to force re-import
- `qml/pages/RecipeEditorPage.qml` - Fixed frame count display

## Files To Create (Future Simple Editor)

- `src/profile/simpleprofileparams.h` - SimpleProfileParams struct
- `src/profile/simpleprofileparams.cpp` - Serialization, frame generation
- `qml/pages/SimpleProfileEditorPage.qml` - 3-step editor UI
- Updates to `main.qml` for editor routing
