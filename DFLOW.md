# Recipe Editor & Profile Types

This document describes the Recipe Editor and supported profile editor types in Decenza DE1.

## What is the Recipe Editor?

The Recipe Editor provides simplified, coffee-concept-based interfaces for creating espresso profiles. Instead of editing raw machine frames, users adjust intuitive parameters like "infuse pressure" and "pour flow", and the editor automatically generates the underlying DE1 frames.

### Key Insight

**Recipe profiles are NOT a different format** — they're a UI abstraction layer. Recipe profiles are standard `settings_2c` (advanced) profiles with the `advanced_shot` array fully populated. The innovation is in the editor, not the storage format.

## Editor Types

The Recipe Editor supports four editor types, each generating a different frame structure:

| Type | Key | Profile Type | Origin | Description |
|------|-----|-------------|--------|-------------|
| D-Flow | `dflow` | `settings_2c` | Damian Brakel | Flow-driven extraction with pressure limit |
| A-Flow | `aflow` | `settings_2c` | Janek | Hybrid pressure-then-flow extraction |
| Pressure | `pressure` | `settings_2a` | de1app | Simple pressure profile |
| Flow | `flow` | `settings_2b` | de1app | Simple flow profile |

## Profile Types in de1app

The de1app uses `settings_profile_type` to distinguish profile complexity:

| Type | Name | Has advanced_shot? | Description |
|------|------|-------------------|-------------|
| `settings_2a` | Simple Pressure | Empty `{}` | Basic pressure profile, converted at runtime |
| `settings_2b` | Simple Flow | Empty `{}` | Basic flow profile, converted at runtime |
| `settings_2c` | Advanced | Populated | Full frame control (D-Flow/A-Flow output this) |
| `settings_2c2` | Advanced + Limiter | Populated | Advanced with limiter UI |

## Design Philosophy

1. **Simplicity First** — Intuitive parameters vs raw frame fields
2. **Live Preview** — Graph updates as you adjust
3. **Backward Compatible** — Saves both recipe params AND generated frames
4. **Escape Hatch** — Can convert to advanced frames for fine-tuning

---

## Recipe Parameters

### Core Parameters

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Stop at Weight | `targetWeight` | 36 | 0–100 | g |
| Stop at Volume | `targetVolume` | 0 | 0–200 | mL |
| Dose | `dose` | 18 | 3–40 | g |

### Fill Phase

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Fill Temperature | `fillTemperature` | 88 | 80–100 | °C |
| Fill Pressure | `fillPressure` | 3.0 | 0–12 | bar |
| Fill Flow | `fillFlow` | 8.0 | 0–10 | mL/s |
| Fill Timeout | `fillTimeout` | 25 | 0+ | s |
| Fill Exit Pressure | `fillExitPressure` | 3.0 | 0–12 | bar |

### Infuse Phase

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Infuse Enabled | `infuseEnabled` | true | — | bool |
| Infuse Pressure | `infusePressure` | 3.0 | 0–6 | bar |
| Infuse Time | `infuseTime` | 20 | 0–60 | s |
| Infuse by Weight | `infuseByWeight` | false | — | bool |
| Infuse Weight | `infuseWeight` | 4.0 | 0–20 | g |
| Infuse Volume | `infuseVolume` | 100 | 10–200 | mL |
| Bloom Enabled | `bloomEnabled` | false | — | bool |
| Bloom Time | `bloomTime` | 10 | 0+ | s |

### Pour Phase (D-Flow / A-Flow)

Pour is always flow-driven with a pressure limit (matching de1app D-Flow/A-Flow model).
`pourFlow` = flow setpoint, `pourPressure` = pressure cap (max_flow_or_pressure).

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Pour Temperature | `pourTemperature` | 93 | 80–100 | °C |
| Pour Pressure | `pourPressure` | 9.0 | 1–12 | bar |
| Pour Flow | `pourFlow` | 2.0 | 0.1–8 | mL/s |
| Ramp Enabled | `rampEnabled` | true | — | bool |
| Ramp Time | `rampTime` | 5.0 | 0–30 | s |

### A-Flow Specific

| Parameter | Key | Default | Description |
|-----------|-----|---------|-------------|
| Ramp Down | `rampDownEnabled` | false | Split pressure ramp into Up + Decline phases (doubles/halves `rampTime`) |
| Flow Up | `flowExtractionUp` | true | Smooth flow ramp during extraction (vs flat) |
| 2nd Fill | `secondFillEnabled` | false | Add 2nd Fill (15s) + Pause (15s) frames before pressure ramp |

### Decline Phase (D-Flow)

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Decline Enabled | `declineEnabled` | false | — | bool |
| Decline To | `declineTo` | 1.0 | 0–10 | mL/s |
| Decline Time | `declineTime` | 30 | 0+ | s |

### Simple Profile Parameters (Pressure / Flow editors)

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Preinfusion Time | `preinfusionTime` | 20 | 0+ | s |
| Preinfusion Flow Rate | `preinfusionFlowRate` | 8.0 | 0–10 | mL/s |
| Preinfusion Stop Pressure | `preinfusionStopPressure` | 4.0 | 0–12 | bar |
| Hold Time | `holdTime` | 10 | 0+ | s |
| Espresso Pressure | `espressoPressure` | 8.4 | 0–12 | bar |
| Hold Flow | `holdFlow` | 2.2 | 0–10 | mL/s |
| Decline Time | `simpleDeclineTime` | 30 | 0+ | s |
| Pressure End | `pressureEnd` | 6.0 | 0–12 | bar |
| Flow End | `flowEnd` | 1.8 | 0–10 | mL/s |
| Limiter Value | `limiterValue` | 3.5 | 0–12 | — |
| Limiter Range | `limiterRange` | 1.0 | 0–10 | — |

### Per-Step Temperatures (Pressure / Flow editors)

| Parameter | Key | Default | Unit |
|-----------|-----|---------|------|
| Start Temperature | `tempStart` | 90 | °C |
| Preinfuse Temperature | `tempPreinfuse` | 90 | °C |
| Hold Temperature | `tempHold` | 90 | °C |
| Decline Temperature | `tempDecline` | 90 | °C |

---

## Frame Generation

### D-Flow Frames

```
Fill → [Bloom] → [Infuse] → [Ramp] → Pour → [Decline]
```

4–6 frames depending on optional phases:

| Frame | Pump | Key Values | Exit | Optional |
|-------|------|-----------|------|----------|
| Fill | flow | flow=fillFlow, pressure=fillPressure | pressure_over fillExitPressure | No |
| Bloom | flow | flow=0 (rest) | pressure_under 0.5 | bloomEnabled |
| Infuse | pressure | pressure=infusePressure | time or weight | infuseEnabled |
| Ramp | flow | flow=pourFlow, pressure=pourPressure, smooth | none (fixed duration) | rampEnabled |
| Pour | flow | flow=pourFlow, pressure=pourPressure | none (weight stops shot) | No |
| Decline | flow | flow=declineTo, smooth | none (time/weight) | declineEnabled |

### A-Flow Frames

```
Pre Fill → Fill → [Infuse] → 2nd Fill → Pause → Pressure Up → Pressure Decline → Flow Start → Flow Extraction
```

Up to 9 frames (matching de1app A-Flow structure):

| # | Frame | Pump | Key Values | Exit | Notes |
|---|-------|------|-----------|------|-------|
| 0 | Pre Fill | flow | flow=8, 1s | none | DE1 "skip first step" workaround |
| 1 | Fill | flow | flow=fillFlow | pressure_over | Same as D-Flow Fill |
| 2 | Infuse | pressure | pressure=infusePressure | time/weight | Optional (infuseEnabled) |
| 3 | 2nd Fill | flow | flow=8, pressure cap=3 | pressure_over 2.5 | 15s when secondFillEnabled, else 0s |
| 4 | Pause | pressure | pressure=1 | flow_under 1.0 | 15s when secondFillEnabled, else 0s |
| 5 | Pressure Up | pressure | pressure=pourPressure, smooth | flow_over pourFlow | rampTime (or rampTime/2 when rampDownEnabled) |
| 6 | Pressure Decline | pressure | pressure→1 bar, smooth | flow_under pourFlow+0.1 | rampTime/2 when rampDownEnabled, else 0s |
| 7 | Flow Start | flow | flow=pourFlow | none (instant) | Transition to flow control |
| 8 | Flow Extraction | flow | flow=pourFlow, limiter=pourPressure | none (weight stops) | smooth when flowExtractionUp, else fast |

#### A-Flow Toggle Effects

**Ramp Down** (`rampDownEnabled`):
- OFF: Pressure Up gets full `rampTime`, Pressure Decline gets 0s (exit condition only)
- ON: `rampTime` is doubled by the UI; Pressure Up and Decline each get `rampTime/2`
- Graph shows split ramp curve when enabled

**Flow Up** (`flowExtractionUp`):
- ON (default): Flow Extraction uses `smooth` transition (flow ramps up gradually)
- OFF: Flow Extraction uses `fast` transition (flat flow)

**2nd Fill** (`secondFillEnabled`):
- OFF: 2nd Fill and Pause frames have 0s duration (skipped immediately)
- ON: 2nd Fill gets 15s, Pause gets 15s — adds a second puck saturation + rest before pressure ramp

### Pressure Profile Frames (settings_2a)

```
[Preinfusion Boost] → Preinfusion → [Forced Rise] → Hold → [Forced Rise] → Decline
```

Matches de1app's `pressure_to_advanced_list()`:

| Frame | Pump | Key Values | Notes |
|-------|------|-----------|-------|
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart ≠ tempPreinfuse |
| Preinfusion | flow | tempPreinfuse, exit pressure_over | |
| Forced Rise | pressure | espressoPressure, 3s, no limiter | When holdTime > 3 |
| Hold | pressure | espressoPressure, with limiter | |
| Forced Rise | pressure | espressoPressure, 3s | When holdTime was short and declineTime > 3 |
| Decline | pressure | pressureEnd, smooth, with limiter | When simpleDeclineTime > 0 |

### Flow Profile Frames (settings_2b)

```
[Preinfusion Boost] → Preinfusion → Hold → Decline
```

Matches de1app's `flow_to_advanced_list()`:

| Frame | Pump | Key Values | Notes |
|-------|------|-----------|-------|
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart ≠ tempPreinfuse |
| Preinfusion | flow | tempPreinfuse, exit pressure_over | |
| Hold | flow | holdFlow, with limiter | When holdTime > 0 |
| Decline | flow | flowEnd, smooth, with limiter | When holdTime > 0 |

---

## JSON Format

Recipe profiles store both the recipe parameters and generated frames:

```json
{
  "title": "A-Flow / default-medium",
  "author": "Recipe Editor",
  "beverage_type": "espresso",
  "profile_type": "settings_2c",
  "target_weight": 36.0,
  "espresso_temperature": 93.0,
  "mode": "frame_based",

  "is_recipe_mode": true,
  "recipe": {
    "editorType": "aflow",
    "targetWeight": 36.0,
    "targetVolume": 0.0,
    "dose": 18.0,
    "fillTemperature": 88.0,
    "fillPressure": 3.0,
    "fillFlow": 8.0,
    "fillTimeout": 25.0,
    "fillExitPressure": 3.0,
    "infuseEnabled": true,
    "infusePressure": 3.0,
    "infuseTime": 20.0,
    "infuseByWeight": false,
    "infuseWeight": 4.0,
    "infuseVolume": 100.0,
    "bloomEnabled": false,
    "bloomTime": 10.0,
    "pourTemperature": 93.0,
    "pourPressure": 9.0,
    "pourFlow": 2.0,
    "rampEnabled": true,
    "rampTime": 5.0,
    "rampDownEnabled": false,
    "flowExtractionUp": true,
    "secondFillEnabled": false,
    "declineEnabled": false,
    "declineTo": 1.0,
    "declineTime": 30.0
  },

  "steps": [
    { "name": "Pre Fill", "pump": "flow", "flow": 8.0, "..." : "..." },
    { "name": "Fill", "pump": "flow", "..." : "..." },
    { "name": "Infuse", "pump": "pressure", "..." : "..." },
    { "name": "2nd Fill", "pump": "flow", "seconds": 0, "..." : "..." },
    { "name": "Pause", "pump": "pressure", "seconds": 0, "..." : "..." },
    { "name": "Pressure Up", "pump": "pressure", "..." : "..." },
    { "name": "Pressure Decline", "pump": "pressure", "..." : "..." },
    { "name": "Flow Start", "pump": "flow", "..." : "..." },
    { "name": "Flow Extraction", "pump": "flow", "..." : "..." }
  ]
}
```

This dual storage ensures:
- Recipe profiles work on older versions (they just see the frames)
- Recipe parameters are preserved for re-editing
- Advanced users can convert to pure frame mode

---

## File Structure

```
src/profile/
├── recipeparams.h          # RecipeParams struct + EditorType enum
├── recipeparams.cpp        # JSON/QVariantMap serialization + validation
├── recipegenerator.h       # Frame generation interface
├── recipegenerator.cpp     # Frame generation for all 4 editor types
├── profile.h               # Extended with recipe support
└── profile.cpp

qml/pages/
├── RecipeEditorPage.qml    # Main recipe editor UI (D-Flow + A-Flow)
└── ProfileEditorPage.qml   # Advanced frame editor (existing)

qml/components/
├── RecipeSection.qml       # Section with title header
├── RecipeRow.qml           # Label + input row
├── ValueInput.qml          # Slider/stepper input control
└── PresetButton.qml        # Preset selector
```

## References

- [D-Flow GitHub Repository](https://github.com/Damian-AU/D_Flow_Espresso_Profile)
- [de1app Profile System](https://github.com/decentespresso/de1app/blob/main/de1plus/profile.tcl)
