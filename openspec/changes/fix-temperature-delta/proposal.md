# Change: Apply Temperature Overrides as Delta Offset

## Why
The profile's `espressoTemperature` (first frame temp) is the reference point, but individual frames can have different temperatures (e.g., preinfusion at 93°C, extraction at 88°C). Currently, both the Profile Editor "All temps" field and the brew temperature override set ALL frames to the same absolute value, destroying these per-frame differences. The correct behavior is to compute a delta from the reference temperature and apply it as an offset to each frame, preserving relative temperature relationships.

## What Changes
- **Temperature override** (`uploadCurrentProfile()`): compute delta = override - profile.espressoTemperature, apply offset to each frame
- **Profile Editor global temp** (`ProfileEditorPage.qml`): compute delta = newValue - firstFrame.temperature, apply offset to each frame
- **BrewDialog display**: show the override as the global temp value (user thinks in terms of "I want 90°C base"), internally stored and applied as delta

## Impact
- Affected specs: `brew-overrides` (MODIFIED: temperature override scenario)
- Affected code:
  - `src/controllers/maincontroller.cpp` — `uploadCurrentProfile()` override logic
  - `qml/pages/ProfileEditorPage.qml` — global temperature `onValueModified` handler
