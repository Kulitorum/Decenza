# D-Flow and Recipe Editor

This document describes D-Flow profile support and the Recipe Editor feature in Decenza DE1.

## What is D-Flow?

D-Flow is a plugin for the de1app created by Damian Brakel that provides a simplified, coffee-concept-based interface for creating Londinium-style espresso profiles. Instead of editing raw machine frames, users adjust intuitive parameters like "infuse pressure" and "pour flow", and D-Flow automatically generates the underlying DE1 frames.

### Key Insight

**D-Flow is NOT a different profile format** - it's a UI abstraction layer. D-Flow profiles are standard `settings_2c` (advanced) profiles with the `advanced_shot` array fully populated. The innovation is in the editor, not the storage format.

## de1app D-Flow Frame Structure

D-Flow always produces exactly **3 frames**: Filling, Infusing, Pouring. The `update_D-Flow` proc modifies these frames in-place when the user adjusts parameters.

```
┌─────────────────────────────────────────────────────────────┐
│                     D-Flow Architecture                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   User edits "coffee concepts":     Generated frames:        │
│   ┌──────────────────────────┐     ┌────────────────────┐   │
│   │ • Fill temp: 86°C        │     │ Frame 0: Filling   │   │
│   │ • Infuse pressure: 3 bar │ ──► │ Frame 1: Infusing  │   │
│   │ • Pour flow: 1.7 mL/s    │     │ Frame 2: Pouring   │   │
│   │ • Pour pressure: 8.5 bar │     └────────────────────┘   │
│   └──────────────────────────┘              │               │
│                                              ▼               │
│                                     ┌────────────────────┐   │
│                                     │ BLE Upload to DE1  │   │
│                                     └────────────────────┘   │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### de1app `update_D-Flow` Field Mapping

The `update_D-Flow` proc reads UI variables and modifies specific fields in each frame. Fields not listed below are **never modified** and retain their template values.

**Frame 0 — Filling** (pressure pump, exits on pressure):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Dflow_filling_temperature` | Fill temperature |
| `pressure` | `Dflow_soaking_pressure` | Soak pressure |
| `exit_pressure_over` | `soaking_pressure / 2` (min 0.9) | Calculated from soak pressure |

**Frame 1 — Infusing** (pressure pump, no machine exit):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Dflow_pouring_temperature` | Uses **pour** temp, not fill |
| `pressure` | `Dflow_soaking_pressure` | Same as fill pressure |
| `seconds` | `Dflow_soaking_seconds` | Soak duration |
| `volume` | `Dflow_soaking_volume` | Max infuse volume |
| `weight` | `Dflow_soaking_weight` | Weight-based exit (app-side) |

**Frame 2 — Pouring** (flow pump, pressure limiter):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Dflow_pouring_temperature` | Pour temperature |
| `flow` | `Dflow_pouring_flow` | Pour flow rate |
| `max_flow_or_pressure` | `Dflow_pouring_pressure` | Pressure limiter |

### Template Constants (never modified by update_D-Flow)

These fields are set once in `set_Dflow_default` and never changed:

| Frame | Field | Value | Notes |
|-------|-------|-------|-------|
| Filling | `pump` | `pressure` | |
| Filling | `flow` | `8` | |
| Filling | `exit_flow_over` | `6` | |
| Filling | `volume` | `100` | Safety limit |
| Filling | `seconds` | `25` | Timeout |
| Filling | `max_flow_or_pressure` | `0` | No extension limiter |
| Filling | `max_flow_or_pressure_range` | `0.2` | |
| Infusing | `pump` | `pressure` | |
| Infusing | `flow` | `8` | |
| Infusing | `exit_if` | `0` | Dead exit fields below |
| Infusing | `exit_type` | `pressure_over` | Dead |
| Infusing | `max_flow_or_pressure` | `0` | No extension limiter |
| Infusing | `max_flow_or_pressure_range` | `0.2` | |
| Pouring | `pump` | `flow` | |
| Pouring | `pressure` | `4.8` | Vestigial, never updated |
| Pouring | `seconds` | `127` | Max, weight stops the shot |
| Pouring | `volume` | `0` | |
| Pouring | `exit_if` | `0` | Dead exit fields below |
| Pouring | `exit_type` | `flow_over` | Dead |
| Pouring | `exit_flow_over` | `2.80` | Dead |
| Pouring | `exit_pressure_over` | `11` | Dead |
| Pouring | `max_flow_or_pressure_range` | `0.2` | |

## A-Flow

A-Flow is a variant of D-Flow created by Janek. It uses a hybrid pressure-then-flow extraction approach: pressure ramps up to build flow, then transitions to flow control for extraction.

### Frame Structure

A-Flow always produces exactly **6 frames**: Fill, Infuse, Pressure Up, Pressure Decline, Flow Start, Flow Extraction.

```
┌─────────────────────────────────────────────────────────────┐
│                     A-Flow Architecture                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   User edits:                      Generated frames:         │
│   ┌──────────────────────────┐     ┌──────────────────────┐ │
│   │ • Fill temp: 95°C        │     │ Frame 0: Fill        │ │
│   │ • Infuse pressure: 3 bar │     │ Frame 1: Infuse      │ │
│   │ • Pour pressure: 10 bar  │ ──► │ Frame 2: Pressure Up │ │
│   │ • Pour flow: 2.0 mL/s   │     │ Frame 3: P. Decline  │ │
│   │ • Ramp time: 10s         │     │ Frame 4: Flow Start  │ │
│   └──────────────────────────┘     │ Frame 5: Extraction  │ │
│                                     └──────────────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Key Differences from D-Flow

| Aspect | D-Flow | A-Flow |
|--------|--------|--------|
| Fill pump | pressure | flow (with 8.0 bar pressure limiter) |
| Fill name | "Filling" | "Fill" |
| Infuse flow | 8.0 | 0.0 (pressure hold) |
| Infuse temperature | pourTemperature | fillTemperature |
| Infuse limiter | 0 (none) | 1.0 (flow limiter) |
| Range (all frames) | 0.2 | 0.6 |
| Extraction | Single flow frame | Pressure ramp → Flow control |

### de1app `update_A-Flow` Field Mapping

Fields not listed below retain their template values.

**Frame 0 — Fill** (flow pump, pressure limiter at 8.0):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_filling_temperature` | Fill temperature |

**Frame 1 — Infuse** (pressure pump, flow=0):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_filling_temperature` | Uses **fill** temp (not pour) |
| `pressure` | `Aflow_soaking_pressure` | Soak pressure |
| `seconds` | `Aflow_soaking_seconds` | Soak duration |
| `volume` | `Aflow_soaking_volume` | Max infuse volume |
| `weight` | `Aflow_soaking_weight` | Weight-based exit (app-side) |

**Frame 2 — Pressure Up** (pressure pump, smooth):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_pouring_temperature` | Pour temperature |
| `pressure` | `Aflow_pouring_pressure` | Pour pressure |
| `seconds` | `ramp_updown_seconds / 2` (if ramp_down) or full | Split when ramp_down_enabled |
| `exit_flow_over` | `pourFlow * 2` (if ramp_down) or `pourFlow` | |

**Frame 3 — Pressure Decline** (pressure pump, smooth):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_pouring_temperature` | Pour temperature |
| `exit_flow_under` | `Aflow_pouring_flow + 0.1` | |
| `seconds` | remaining ramp time (if ramp_down) or 0 | |

**Frame 4 — Flow Start** (flow pump, conditionally activated):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_pouring_temperature` | Pour temperature |
| `flow` | `Aflow_pouring_flow` | User's target flow |
| `seconds` | 10 (if rampTime < 1) or 0 | Activated when no ramp |
| `exit_if` | 1 (if rampTime < 1) or 0 | |
| `exit_flow_over` | `pourFlow - 0.1` (if activated) | |

**Frame 5 — Flow Extraction** (flow pump, smooth):
| Field | Source | Notes |
|-------|--------|-------|
| `temperature` | `Aflow_pouring_temperature` | Pour temperature |
| `flow` | `pourFlow * 2` (if flow_extraction_up) or 0 | |
| `max_flow_or_pressure` | `Aflow_pouring_pressure` | Pressure limiter |

### Template Constants (never modified by update_A-Flow)

| Frame | Field | Value | Notes |
|-------|-------|-------|-------|
| Fill | `pump` | `flow` | NOT pressure (differs from D-Flow) |
| Fill | `max_flow_or_pressure` | `8.0` | Pressure limiter |
| Fill | `max_flow_or_pressure_range` | `0.6` | |
| Infuse | `pump` | `pressure` | |
| Infuse | `flow` | `0` | Zero flow (pressure hold) |
| Infuse | `max_flow_or_pressure` | `1.0` | Flow limiter |
| Infuse | `max_flow_or_pressure_range` | `0.6` | |
| Infuse | `exit_pressure_over` | `3.0` | Dead (exit_if=0) |
| Pressure Up | `exit_pressure_over` | `8.5` | |
| Pressure Decline | `pressure` | `1.0` | Target decline pressure |
| Pressure Decline | `exit_flow_over` | `3.0` | |
| Pressure Decline | `exit_pressure_over` | `11.0` | |
| Pressure Decline | `exit_pressure_under` | `1.0` | |
| Flow Start | `pressure` | `3.0` | Vestigial |
| Flow Extraction | `pressure` | `3.0` | Vestigial |

### Conditional Logic

- **`ramp_down_enabled`**: Splits `rampTime` between Pressure Up (`rampTime/2`) and Pressure Decline (`rampTime - rampTime/2`). When false, all time goes to Up, Decline gets 0.
- **`flow_extraction_up`**: When true, extraction flow = `pourFlow * 2` (ramp up during extraction). When false, extraction flow = 0 (pressure-only extraction via limiter).
- **Conditional Flow Start**: When `rampTime < 1`, Flow Start becomes active (`exit_if=1`, `seconds=10`, exits on `flow_over` at `pourFlow - 0.1`). Otherwise it's a zero-second passthrough.

### pourFlow Convention

For A-Flow, `pourFlow` stores the user's target flow rate (de1app's `Aflow_pouring_flow`), which goes into the Flow Start frame. The extraction flow is derived:
- `flowExtractionUp=true` → extraction flow = `pourFlow * 2`
- `flowExtractionUp=false` → extraction flow = 0

This matches de1app where the "Pouring Flow" slider controls the target flow, not the extraction ramp.

---

## Profile Types in de1app

The de1app uses `settings_profile_type` to distinguish profile complexity:

| Type | Name | Has advanced_shot? | Description |
|------|------|-------------------|-------------|
| `settings_2a` | Simple Pressure | Empty `{}` | Basic pressure profile, converted at runtime |
| `settings_2b` | Simple Flow | Empty `{}` | Basic flow profile, converted at runtime |
| `settings_2c` | Advanced | Populated | Full frame control (D-Flow outputs this) |
| `settings_2c2` | Advanced + Limiter | Populated | Advanced with limiter UI |

### Current Support Status

| Profile Type | Import | Execute | Edit |
|--------------|--------|---------|------|
| `settings_2c` (Advanced/D-Flow) | Yes | Yes | Yes (frame editor) |
| `settings_2c2` (Advanced + Limiter) | Yes | Yes | Yes (frame editor) |
| `settings_2a` (Simple Pressure) | **No** | N/A | N/A |
| `settings_2b` (Simple Flow) | **No** | N/A | N/A |

Simple profiles (`settings_2a`, `settings_2b`) have empty `advanced_shot` arrays and require conversion functions that we haven't implemented yet.

## D-Flow Parameters (Reference)

These are the user-adjustable parameters in the original D-Flow plugin:

| Parameter | Variable | Default | Range | Description |
|-----------|----------|---------|-------|-------------|
| Dose Weight | `grinder_dose_weight` | 18g | 0-40g | Input coffee weight |
| Fill Temperature | `Dflow_filling_temperature` | 86°C | 80-105°C | Initial fill temp |
| Infuse Pressure | `Dflow_soaking_pressure` | 3.0 bar | 1.2-12 bar | Soak/preinfusion pressure |
| Infuse Time | `Dflow_soaking_seconds` | 20s | 1-127s | Soak duration |
| Infuse Volume | `Dflow_soaking_volume` | 100ml | 50-127ml | Max infuse volume |
| Infuse Weight | `Dflow_soaking_weight` | 4.0g | 0-500g | Weight to exit infuse |
| Pour Temperature | `Dflow_pouring_temperature` | 88°C | 80-105°C | Extraction temp |
| Pour Flow | `Dflow_pouring_flow` | 1.7 mL/s | 0.1-8 mL/s | Extraction flow rate |
| Pour Pressure | `Dflow_pouring_pressure` | 4.8 bar | 1-12 bar | Max pour pressure (limiter) |
| Target Volume | `final_desired_shot_volume_advanced` | 54ml | 0-1000ml | Stop at volume |
| Target Weight | `final_desired_shot_weight_advanced` | 50g | 0-1000g | Stop at weight |

---

# Recipe Editor

The Recipe Editor is our implementation of a D-Flow-style simplified profile editor. It provides intuitive coffee-concept controls that automatically generate DE1 frames.

## Design Philosophy

1. **de1app Compatible** - Generates identical 3-frame structure (Filling, Infusing, Pouring)
2. **Simplicity First** - 12 parameters vs 20+ frame fields
3. **Live Preview** - Graph updates as you adjust
4. **Backward Compatible** - Saves both recipe params AND generated frames
5. **Escape Hatch** - Can convert to advanced frames for fine-tuning

## Recipe Parameters

### Fill Phase

| Parameter | Key | Default | Range | Step | Unit |
|-----------|-----|---------|-------|------|------|
| Fill Pressure | `fillPressure` | 3.0 | 1-6 | 0.1 | bar |
| Fill Temperature | `fillTemperature` | 88 | 80-105 | 0.5 | °C |
| Fill Flow | `fillFlow` | 8.0 | 1-8 | 0.1 | mL/s |
| Fill Exit Pressure | `fillExitPressure` | 1.5 | 0.5-6 | 0.1 | bar |
| Fill Timeout | `fillTimeout` | 25 | 5-60 | 1 | s |

### Infuse Phase

| Parameter | Key | Default | Range | Step | Unit |
|-----------|-----|---------|-------|------|------|
| Infuse Enabled | `infuseEnabled` | true | - | - | bool |
| Infuse Pressure | `infusePressure` | 3.0 | 1-6 | 0.1 | bar |
| Infuse Time | `infuseTime` | 20 | 0-60 | 1 | s |
| Infuse by Weight | `infuseByWeight` | false | - | - | bool |
| Infuse Weight | `infuseWeight` | 4.0 | 0-20 | 0.5 | g |
| Infuse Volume | `infuseVolume` | 100 | 50-127 | 1 | mL |

### Pour Phase

| Parameter | Key | Default | Range | Step | Unit |
|-----------|-----|---------|-------|------|------|
| Pour Flow | `pourFlow` | 1.7 | 0.1-8 | 0.1 | mL/s |
| Pour Pressure | `pourPressure` | 8.5 | 1-12 | 0.1 | bar |
| Pour Temperature | `pourTemperature` | 88 | 80-105 | 0.5 | °C |

### Targets

| Parameter | Key | Default | Range | Step | Unit |
|-----------|-----|---------|-------|------|------|
| Target Weight | `targetWeight` | 50 | 0-100 | 1 | g |
| Target Volume | `targetVolume` | 54 | 0-1000 | 1 | mL |
| Dose | `dose` | 18 | 0-40 | 0.1 | g |

### Decenza Extras (not in de1app)

| Parameter | Key | Default | Description |
|-----------|-----|---------|-------------|
| Bloom Enabled | `bloomEnabled` | false | CO2 release pause before infuse |
| Bloom Time | `bloomTime` | 10 | Bloom duration (seconds) |
| Decline Enabled | `declineEnabled` | false | Flow decline after pour |
| Decline To | `declineTo` | 1.0 | Target decline flow (mL/s) |
| Decline Time | `declineTime` | 30 | Decline duration (seconds) |

## Frame Generation

The Recipe Editor generates 3 core frames from recipe parameters, plus optional Bloom and Decline:

```
Recipe Parameters          Generated Frames
─────────────────          ────────────────
Fill: 3 bar, 25s    ───►   Frame 0: Filling
                           • pump: pressure, pressure: 3.0
                           • exit: pressure_over at 1.5 bar
                           • max_flow_or_pressure: 0

                    ───►   [Bloom] (if enabled, Decenza extra)
                           • pump: flow, flow: 0 (rest)
                           • exit: pressure_under 0.5 bar

Infuse: 3 bar, 60s ───►   Frame 1: Infusing
                           • pump: pressure, pressure: 3.0
                           • temp: pourTemperature (not fill!)
                           • max_flow_or_pressure: 0

Pour: 1.7 mL/s     ───►   Frame 2: Pouring
                           • pump: flow, flow: 1.7
                           • pressure: 4.8 (vestigial)
                           • max_flow_or_pressure: 8.5 (limiter)
                           • seconds: 127 (weight stops shot)

                    ───►   [Decline] (if enabled, Decenza extra)
                           • pump: flow, flow: declineTo
                           • transition: smooth
```

## JSON Format

Recipe profiles store both the recipe parameters and generated frames:

```json
{
  "title": "D-Flow / default",
  "author": "Decent (D-Flow by Damian)",
  "beverage_type": "espresso",
  "profile_type": "settings_2c",
  "target_weight": 50.0,
  "target_volume": 54.0,
  "espresso_temperature": 88.0,
  "mode": "frame_based",
  "preinfuse_frame_count": 2,

  "is_recipe_mode": true,
  "recipe": {
    "editorType": "dflow",
    "fillPressure": 3.0,
    "fillTemperature": 88.0,
    "fillExitPressure": 1.5,
    "fillFlow": 8.0,
    "fillTimeout": 25.0,
    "infuseEnabled": true,
    "infusePressure": 3.0,
    "infuseByWeight": true,
    "infuseWeight": 4.0,
    "infuseTime": 60.0,
    "infuseVolume": 100.0,
    "pourFlow": 1.7,
    "pourPressure": 8.5,
    "pourTemperature": 88.0,
    "targetWeight": 50.0,
    "targetVolume": 54.0,
    "dose": 18.0
  },

  "steps": [
    {
      "name": "Filling", "pump": "pressure", "pressure": 3.0, "flow": 8.0,
      "temperature": 88, "seconds": 25, "volume": 100, "transition": "fast",
      "sensor": "coffee", "exit_if": true, "exit_type": "pressure_over",
      "exit_pressure_over": 1.5, "exit_flow_over": 6.0,
      "max_flow_or_pressure": 0, "max_flow_or_pressure_range": 0.2
    },
    {
      "name": "Infusing", "pump": "pressure", "pressure": 3.0, "flow": 8.0,
      "temperature": 88, "seconds": 60, "volume": 100, "transition": "fast",
      "sensor": "coffee", "exit_if": false, "exit_type": "pressure_over",
      "exit_pressure_over": 3.0, "exit_flow_over": 6.0, "exit_weight": 4.0,
      "max_flow_or_pressure": 0, "max_flow_or_pressure_range": 0.2
    },
    {
      "name": "Pouring", "pump": "flow", "flow": 1.7, "pressure": 4.8,
      "temperature": 88, "seconds": 127, "volume": 0, "transition": "fast",
      "sensor": "coffee", "exit_if": false, "exit_type": "flow_over",
      "exit_flow_over": 2.80, "exit_pressure_over": 11,
      "max_flow_or_pressure": 8.5, "max_flow_or_pressure_range": 0.2
    }
  ]
}
```

This dual storage ensures:
- Recipe profiles work on older versions (they just see the frames)
- Recipe parameters are preserved for re-editing
- Advanced users can convert to pure frame mode

## File Structure

```
src/profile/
├── recipeparams.h          # RecipeParams struct
├── recipeparams.cpp        # JSON serialization
├── recipegenerator.h       # Frame generation interface
├── recipegenerator.cpp     # Frame generation algorithm
├── profile.h               # Extended with recipe support
└── profile.cpp

qml/pages/
├── RecipeEditorPage.qml    # Main recipe editor UI
└── ProfileEditorPage.qml   # Advanced frame editor (existing)

qml/components/
├── RecipeSection.qml       # Collapsible section
├── RecipeRow.qml           # Label + input row
└── PresetButton.qml        # Preset selector
```

## References

- [D-Flow GitHub Repository](https://github.com/Damian-AU/D_Flow_Espresso_Profile)
- [D-Flow Blog Post](https://decentespresso.com/blog/dflow_an_easy_editor_for_the_londinium_family_of_espresso_profiles)
- [de1app Profile System](https://github.com/decentespresso/de1app/blob/main/de1plus/profile.tcl)
