# Recipe Editor & Profile Types

This document describes the Recipe Editor, supported profile editor types, and how Decenza's implementation syncs with de1app's D-Flow and A-Flow editors.

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

## Editor Selection

`MainController::currentEditorType()` determines which editor page opens. Selection is **title-first**:

1. Title starts with `D-Flow/` → D-Flow editor
2. Title starts with `A-Flow/` → A-Flow editor
3. `profile_type` is `settings_2a` → Pressure editor
4. `profile_type` is `settings_2b` → Flow editor
5. Everything else → Advanced frame editor

This matches de1app's convention where D-Flow and A-Flow profiles are identified by their title prefix.

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

### Architecture: Regeneration vs Patching

**de1app uses a patch model**: `update_D-Flow` and `update_A-Flow` read existing frames, modify only the fields exposed in the UI, and write them back. Fields not exposed in the UI (like Fill `volume`, Pouring `seconds`, dead exit values on `exit_if=0` frames) are preserved from the saved profile.

**Decenza uses a regenerative model**: `RecipeGenerator` builds all frames from scratch using recipe parameters. A passthrough mechanism in `Profile::regenerateFromRecipe()` preserves `volume` and `exitWeight` from old frames by matching on frame name, preventing lossy round-trips for fields that RecipeParams doesn't control.

**Metadata-only optimization**: When only non-frame-affecting params change (`targetWeight`, `targetVolume`, `dose`), frame regeneration is skipped entirely. This matches de1app where changing `final_desired_shot_weight` doesn't call `update_D-Flow` / `update_A-Flow`. Implemented in `MainController::uploadRecipeProfile()` via `RecipeParams::frameAffectingFieldsEqual()`.

### D-Flow Frames

```
Filling → [Bloom] → Infusing → Pouring → [Decline]
```

**Always 3 core frames** matching de1app (Filling, Infusing, Pouring). When `infuseEnabled=false`, Infusing is emitted with `seconds=0` (machine skips it), NOT omitted — this preserves the 3-frame structure de1app expects. Bloom and Decline are optional Decenza extras not present in de1app.

#### D-Flow Frame Details (matches `update_D-Flow` in de1app `D_Flow/code.tcl`)

**Frame 0: Filling** — pressure pump to saturate puck

| Field | Value | Source |
|-------|-------|--------|
| name | "Filling" | — |
| pump | "pressure" | — |
| pressure | recipe.infusePressure | de1app: `Dflow_soaking_pressure` |
| flow | recipe.fillFlow | de1app: preserved from profile (default 8.0) |
| temperature | recipe.fillTemperature | de1app: `Dflow_filling_temperature` |
| seconds | recipe.fillTimeout | de1app: preserved from profile (default 25.0) |
| transition | "fast" | — |
| sensor | "coffee" | — |
| volume | 100.0 | de1app: preserved (passthrough handles) |
| weight | 5.0 | de1app: preserved (default 5.0 = app-side fill exit) |
| exit_if | true | — |
| exit_type | "pressure_over" | — |
| exit_pressure_over | formula (see below) | de1app: same formula |
| exit_flow_over | 6.0 | — |
| max_flow_or_pressure | 0.0 | — |
| max_flow_or_pressure_range | 0.2 | — |

**Fill exit_pressure_over formula** (matches `update_D-Flow` line 341-346):
```
if pressure >= 2.8:
    exit = round_to_one_digit(pressure / 2 + 0.6)
else:
    exit = pressure
if exit < 1.2:
    exit = 1.2
```

**Frame 1: Infusing** — hold at soak pressure

| Field | Value | Source |
|-------|-------|--------|
| name | "Infusing" | — |
| pump | "pressure" | — |
| pressure | recipe.infusePressure | de1app: `Dflow_soaking_pressure` |
| flow | 8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Dflow_pouring_temperature` |
| seconds | recipe.infuseTime (0 when disabled) | de1app: `Dflow_soaking_seconds` |
| volume | recipe.infuseVolume (100 when disabled) | de1app: `Dflow_soaking_volume` |
| weight | recipe.infuseWeight | de1app: `Dflow_soaking_weight` (app-side SkipToNext) |
| exit_if | false | — |
| exit_type | "pressure_over" | — |
| exit_pressure_over | recipe.infusePressure | de1app: preserved (default 3.0) |
| max_flow_or_pressure | 0.0 | — |
| max_flow_or_pressure_range | 0.2 | — |

**Frame 2: Pouring** — flow-driven extraction with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| name | "Pouring" | — |
| pump | "flow" | — |
| flow | recipe.pourFlow | de1app: `Dflow_pouring_flow` |
| pressure | 4.8 | de1app: preserved (vestigial) |
| temperature | recipe.pourTemperature | de1app: `Dflow_pouring_temperature` |
| seconds | 127.0 | de1app: preserved (max duration) |
| transition | "fast" | — |
| volume | 0.0 | de1app: preserved (passthrough handles) |
| exit_if | false | — |
| exit_type | "flow_over" | — |
| exit_flow_over | 2.80 | — |
| exit_pressure_over | 11.0 | — |
| max_flow_or_pressure | recipe.pourPressure | de1app: `Dflow_pouring_pressure` |
| max_flow_or_pressure_range | 0.2 | — |

### D-Flow Stock Profiles (from de1app `D_Flow/code.tcl`)

| Profile | Fill pressure | Fill exit | Infuse seconds | Pour flow | Pour pressure | Weight |
|---------|--------------|-----------|----------------|-----------|---------------|--------|
| D-Flow / default | 3.0 bar | 1.5 bar | 60s | 1.7 mL/s | 8.5 bar | 50g |
| D-Flow / Q | 6.0 bar | 3.0 bar | 1s | 1.8 mL/s | 10.0 bar | 36g |
| D-Flow / La Pavoni | 1.2 bar | 1.2 bar | 60s | 2.4 mL/s | 9.0 bar | 46g |

Note: Stock profiles have hand-tuned `exit_pressure_over` values that differ from the formula. The formula produces 2.1 for pressure=3.0 (not the stock 1.5). Both de1app's editor and Decenza apply the formula when the user edits any parameter, so hand-tuned values are overwritten on first edit. This is de1app's intended behavior.

### A-Flow Frames

```
Pre Fill → Fill → Infuse → 2nd Fill → Pause → Pressure Up → Pressure Decline → Flow Start → Flow Extraction
```

**Always 9 frames** (matching de1app). When `infuseEnabled=false`, the Infuse frame is emitted with `seconds=0`, NOT omitted. When `secondFillEnabled=false`, 2nd Fill and Pause have `seconds=0`. De1app's `set_profile_index` uses `> 8` frames to detect new-format profiles; omitting frames would cause it to fall back to 6-frame (old format) indexing.

#### A-Flow Frame Details (matches `update_A-Flow` in de1app `A_Flow/code.tcl`)

**Frame 0: Pre Fill** — 1s workaround for DE1 "skip first step" bug

| Field | Value |
|-------|-------|
| pump | "flow" |
| flow | 8.0, pressure=3.0 |
| temperature | recipe.fillTemperature |
| seconds | 1.0 |
| exit_if | false |
| max_flow_or_pressure | 8.0 (range 0.6) |

**Frame 1: Fill** — flow pump with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| pump | "flow" | — |
| flow | recipe.fillFlow | de1app: preserved (not modified by `update_A-Flow`) |
| pressure | recipe.fillPressure | de1app: preserved |
| temperature | recipe.fillTemperature | de1app: `Aflow_filling_temperature` |
| seconds | recipe.fillTimeout | de1app: preserved |
| exit_if | true, exit_type "pressure_over" | — |
| exit_pressure_over | recipe.fillPressure | — |
| max_flow_or_pressure | 8.0 (range 0.6) | — |

**Frame 2: Infuse** — pressure hold with zero flow

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| flow | 0.0 | — |
| pressure | recipe.infusePressure | de1app: `Aflow_soaking_pressure` |
| temperature | recipe.fillTemperature | de1app: `Aflow_filling_temperature` (NOT pour temp) |
| seconds | recipe.infuseTime (0 when disabled) | de1app: `Aflow_soaking_seconds` |
| volume | recipe.infuseVolume (100 when disabled) | de1app: `Aflow_soaking_volume` |
| weight | recipe.infuseWeight | de1app: `Aflow_soaking_weight` (app-side SkipToNext) |
| exit_if | false | — |
| max_flow_or_pressure | 1.0 (range 0.6) | — |

**Frames 3-4: 2nd Fill + Pause** — optional second saturation cycle

| | 2nd Fill | Pause |
|-|----------|-------|
| pump | flow | pressure |
| flow/pressure | flow=8.0 | pressure=1.0, flow=6.0 |
| temperature | pourTemperature (95 when disabled) | pourTemperature (95 when disabled) |
| seconds | 15 (0 when disabled) | 15 (0 when disabled) |
| exit | pressure_over 2.5 | flow_under 1.0 |
| limiter | 3.0 (range 0.6) | 1.0 (range 0.6) |

**Frame 5: Pressure Up** — smooth ramp to pour pressure

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| pressure | recipe.pourPressure | de1app: `Aflow_pouring_pressure` |
| flow | 8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| transition | "smooth" | — |
| seconds | floor(rampTime/2) when rampDown, else rampTime | de1app: `round_to_integer(rampTime/2)` |
| exit_if | true, exit_type "flow_over" | — |
| exit_flow_over | round(pourFlow*2, 1) when rampDown, else round(pourFlow, 1) | de1app: `round_to_one_digits` |
| exit_pressure_over | 8.5 | — |

**Frame 6: Pressure Decline** — decline to 1 bar

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| pressure | 1.0, flow=8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| transition | "smooth" | — |
| seconds | rampTime - floor(rampTime/2) when rampDown, else 0 | de1app: `round_to_integer(rampTime/2 + rampTime%2)` |
| exit_if | true, exit_type "flow_under" | — |
| exit_flow_under | round(pourFlow + 0.1, 1) | de1app: `round_to_one_digits` |
| exit_flow_over | 3.0 | — |
| exit_pressure_over | 11.0, exit_pressure_under=1.0 | — |

Note: Integer rounding gives the remainder second to Decline (e.g., rampTime=11 → Up=5, Decline=6).

**Frame 7: Flow Start** — conditionally activated

| State | Condition | seconds | exit |
|-------|-----------|---------|------|
| Passthrough | pressureUpSeconds >= 1 | 0 | exit_if=false |
| Activated | pressureUpSeconds < 1 | 10 | exit_if=true, flow_over round(pourFlow-0.1, 1) |

When activated (ramp disabled or very short), this frame waits for flow to stabilize before extraction. When passthrough, the machine skips it immediately (seconds=0).

**Frame 8: Flow Extraction** — main extraction with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| pump | "flow" | — |
| flow | round(pourFlow*2, 1) when flowExtractionUp, else 0 | de1app: `round_to_one_digits` |
| pressure | 3.0 (vestigial) | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| seconds | 60.0 | — |
| transition | "smooth" | — |
| max_flow_or_pressure | recipe.pourPressure | de1app: `Aflow_pouring_pressure` |
| max_flow_or_pressure_range | 0.6 | — |
| exit_if | false | — |

#### A-Flow Toggle Effects

**Ramp Down** (`rampDownEnabled`):
- OFF: Pressure Up gets full `rampTime`, Pressure Decline gets 0s (exit condition only)
- ON: `rampTime` is doubled by the UI; Pressure Up and Decline each get `rampTime/2`
- Integer rounding: `floor(rampTime/2)` for Up, remainder for Decline

**Flow Up** (`flowExtractionUp`):
- ON (default): Flow Extraction flow = `pourFlow * 2` with smooth transition (ramps up)
- OFF: Flow Extraction flow = 0 (flat, pressure-limited only)

**2nd Fill** (`secondFillEnabled`):
- OFF: 2nd Fill and Pause frames have 0s duration and temperature=95 (skipped immediately)
- ON: 2nd Fill gets 15s, Pause gets 15s, both use pourTemperature

#### Value Rounding

All computed flow exit values use `round_to_one_digits` matching de1app:
- Pressure Up `exit_flow_over`: `round(value * 10) / 10`
- Pressure Decline `exit_flow_under`: `round((pourFlow + 0.1) * 10) / 10`
- Flow Start `exit_flow_over`: `round((pourFlow - 0.1) * 10) / 10`
- Flow Extraction `flow`: `round(pourFlow * 2 * 10) / 10`

### Pressure Profile Frames (settings_2a)

```
[Preinfusion Boost] → Preinfusion → [Forced Rise] → Hold → [Forced Rise] → Decline
```

Matches de1app's `pressure_to_advanced_list()`:

| Frame | Pump | Key Values | Notes |
|-------|------|-----------|-------|
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart != tempPreinfuse |
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
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart != tempPreinfuse |
| Preinfusion | flow | tempPreinfuse, exit pressure_over | |
| Hold | flow | holdFlow, with limiter | When holdTime > 0 |
| Decline | flow | flowEnd, smooth, with limiter | When holdTime > 0 |

---

## Exit Conditions

There are two independent exit condition systems:

### Machine-side exits (pressure/flow)

Controlled by `exit_if` flag and `exit_type` in the BLE frame. The machine autonomously checks and advances frames. Types: `pressure_over`, `pressure_under`, `flow_over`, `flow_under`.

### App-side exits (weight)

Controlled by `weight` field on the frame, INDEPENDENTLY of `exit_if`. The app monitors scale weight and sends `SkipToNext` (0x0E) command. A frame can have no machine exit (`exit_if=false`) with a weight exit (`weight > 0`), or both, or neither.

D-Flow uses weight exits on:
- Filling frame: `weight=5.0` (exit fill early if scale reads 5g)
- Infusing frame: `weight=infuseWeight` (exit infuse at target weight)
- Profile-level `targetWeight`: stops the shot via the app

---

## Decenza vs de1app Default Values

These defaults only apply when creating brand-new profiles, not when editing existing ones:

| Parameter | de1app D-Flow/default | de1app A-Flow stock | Decenza default |
|-----------|----------------------|---------------------|-----------------|
| fillTemperature | 88 | 93 | 88 |
| pourTemperature | 88 | 93 | 93 |
| infuseTime | 60s | 60s | 20s |
| pourFlow | 1.7 mL/s | 2.0 mL/s | 2.0 mL/s |
| pourPressure | 8.5 bar | 10.0 bar | 9.0 bar |
| targetWeight | 50g | 36g | 36g |
| fillTimeout | 25s | 15s | 25s |
| infuseWeight | 4.0g | 3.6g | 4.0g |

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
    "infuseEnabled": true,
    "infusePressure": 3.0,
    "infuseTime": 20.0,
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
├── recipeparams.cpp        # JSON/QVariantMap serialization + validation + frameAffectingFieldsEqual()
├── recipegenerator.h       # Frame generation interface
├── recipegenerator.cpp     # Frame generation for all 4 editor types
├── profile.h               # Extended with recipe support
└── profile.cpp             # regenerateFromRecipe() with passthrough preservation

src/controllers/
└── maincontroller.cpp      # uploadRecipeProfile() with metadata-only optimization
                            # currentEditorType() with title-first detection

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
- de1app D-Flow source: `de1plus/profile_editors/D_Flow/code.tcl`
- de1app A-Flow source: `de1plus/profile_editors/A_Flow/code.tcl`
