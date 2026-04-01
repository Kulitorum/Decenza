# DE1 Profile Porting Guide

## Task
Port DE1 espresso machine profiles from the de1app Tcl format to our Qt app's JSON format.

## Source Location
Tcl profiles are located at: `C:\code\de1app\de1plus\profiles\*.tcl`

## Destination Location
JSON profiles go to: `C:\CODE\de1-qt\resources\profiles\*.json`

After creating a profile, add it to `C:\CODE\de1-qt\resources\resources.qrc`:
```xml
<file>profiles/your_profile_name.json</file>
```

## Tcl Format Structure

The Tcl files contain key-value pairs. The main data is in `advanced_shot` which is a Tcl list of frame dictionaries:

```tcl
advanced_shot {{exit_if 0 flow 6.0 volume 100 ... name {frame name} pressure 1 sensor coffee pump flow ...} {next frame...}}
profile_title {Profile Name}
author Name
espresso_temperature 88.00
final_desired_shot_weight_advanced 45.0
```

## JSON Format Structure

```json
{
    "title": "Profile Name",
    "author": "Author Name",
    "notes": "Description of the profile",
    "beverage_type": "espresso",
    "profile_type": "settings_2c",
    "target_weight": 45.0,
    "target_volume": 42.0,
    "espresso_temperature": 88.0,
    "temperature_presets": [84.0, 86.0, 88.0, 90.0],
    "maximum_pressure": 0,
    "maximum_flow": 0,
    "minimum_pressure": 0.0,
    "preinfuse_frame_count": 2,
    "mode": "frame_based",
    "steps": [
        {
            "name": "Frame Name",
            "temperature": 88.0,
            "sensor": "coffee",
            "pump": "flow",
            "transition": "fast",
            "pressure": 1.0,
            "flow": 6.0,
            "seconds": 20.0,
            "volume": 100,
            "max_flow_or_pressure": 0,
            "max_flow_or_pressure_range": 0.6,
            "exit_if": true,
            "exit_type": "pressure_over",
            "exit_pressure_over": 4.0,
            "exit_pressure_under": 0,
            "exit_flow_over": 0,
            "exit_flow_under": 0
        }
    ]
}
```

## Field Mapping (Tcl → JSON)

| Tcl Field | JSON Field | Notes |
|-----------|------------|-------|
| `profile_title` | `title` | |
| `author` | `author` | |
| `profile_notes` | `notes` | Summarize long notes |
| `espresso_temperature` | `espresso_temperature` | |
| `final_desired_shot_weight_advanced` | `target_weight` | |
| `final_desired_shot_volume` | `target_volume` | |
| `settings_profile_type` | `profile_type` | |

### Frame/Step Field Mapping

| Tcl Field | JSON Field | Notes |
|-----------|------------|-------|
| `name` | `name` | Frame name (may have braces in Tcl: `{frame name}`) |
| `temperature` | `temperature` | |
| `sensor` | `sensor` | "coffee" or "water" |
| `pump` | `pump` | "flow" or "pressure" |
| `transition` | `transition` | "fast" or "smooth" |
| `pressure` | `pressure` | Target pressure in bar |
| `flow` | `flow` | Target flow in mL/s |
| `seconds` | `seconds` | Max duration |
| `volume` | `volume` | Usually 100 (max volume limit) |
| `max_flow_or_pressure` | `max_flow_or_pressure` | Limiter value (0 = disabled) |
| `max_flow_or_pressure_range` | `max_flow_or_pressure_range` | Usually 0.6 |
| `exit_if` | `exit_if` | 0→false, 1→true |
| `exit_type` | `exit_type` | See exit types below |
| `exit_pressure_over` | `exit_pressure_over` | |
| `exit_pressure_under` | `exit_pressure_under` | |
| `exit_flow_over` | `exit_flow_over` | |
| `exit_flow_under` | `exit_flow_under` | |

### Exit Types

**Machine-side exits** (checked by DE1, controlled by `exit_if` flag):
- `pressure_over` - Exit when pressure exceeds threshold
- `pressure_under` - Exit when pressure drops below threshold
- `flow_over` - Exit when flow exceeds threshold
- `flow_under` - Exit when flow drops below threshold

**App-side exits** (checked by app, INDEPENDENT of `exit_if`):
- `weight` - Exit when scale weight reaches threshold

### Weight Exit (IMPORTANT)

The `weight` field (Tcl) or `exit_weight` field (JSON) is **independent** of the `exit_if` flag:

```tcl
# Tcl example - exit_if 0 but weight 3.6 still triggers app-side exit
{exit_if 0 exit_type pressure_over weight 3.6 name Infuse ...}
```

```json
// JSON - both machine and app exits can coexist
{
    "exit_if": true,
    "exit_type": "pressure_over",
    "exit_pressure_over": 4.0,
    "exit_weight": 3.6
}
```

| Tcl Field | JSON Field | Notes |
|-----------|------------|-------|
| `weight` | `exit_weight` | Per-frame weight exit (app-side, independent of exit_if) |

## Parsing Tips

1. **Tcl lists with braces**: `{frame name}` becomes `"frame name"` in JSON
2. **Floating point cleanup**: `6.000000000000007` should become `6.0`
3. **Boolean conversion**: Tcl `0`/`1` becomes JSON `false`/`true`
4. **preinfuse_frame_count**: Use `final_desired_shot_volume_advanced_count_start` from the de1app TCL source. For simple profiles (settings_2a/2b), this is auto-calculated during frame generation. For advanced profiles, use the explicit value (de1app defaults to 0 when missing).
5. **temperature_presets**: Create 4 useful presets around the base temperature

## Example Ported Profile

See `C:\CODE\de1-qt\resources\profiles\easy_blooming_active_pressure_decline.json` for a complete 12-frame example.

## Profiles to Port

Run this to see available profiles:
```
ls C:\code\de1app\de1plus\profiles\*.tcl
```

Priority profiles to port:
- Profiles with "rao" in the name (Scott Rao recipes)
- Profiles with "londinium" in the name
- Profiles with "lever" in the name
- Any profile the user specifically requests
