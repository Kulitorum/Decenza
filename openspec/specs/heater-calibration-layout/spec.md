# heater-calibration-layout Specification

## Purpose
TBD - created by archiving change heater-calibration-inline-labels. Update Purpose after archive.
## Requirements
### Requirement: Parameter labels sit left of their value controls
In the Heater Calibration popup (`calibrationPopup` in `SettingsCalibrationTab.qml`), each of the five parameters (Heater idle temperature, Heater warmup flow rate, Heater test flow rate, Heater test time-out, Fan temperature threshold) SHALL be presented as a single horizontal row: the descriptive label on the left and its `ValueInput` control on the right of the same row. Labels SHALL keep their existing translation keys, caption font, and per-parameter colors (temperature/flow/secondary), and SHALL word-wrap within the row when the translated text does not fit on one line.

#### Scenario: Label and value share a row
- **WHEN** the Heater Calibration popup is opened
- **THEN** each parameter's label renders on the same horizontal row as its value control, label left, value right
- **AND** no parameter label renders on its own row above the value control

#### Scenario: Long translation wraps instead of clipping
- **WHEN** the active language renders a parameter label wider than the space left of the value control
- **THEN** the label wraps to additional lines within its row
- **AND** the value control remains fully visible and right-aligned

### Requirement: Dialog fits without scrolling at typical sizes
The Heater Calibration popup SHALL be short enough that its full content — title, all five parameter rows, the "Defaults for cafe" button, and the Cancel/Done row — is visible without scrolling whenever the popup's height cap (`parent.height * 0.85`) is not exceeded by the compacted layout, which SHALL hold on the reference tablet resolution (1280×800 landscape) and typical desktop windows. A vertical scroll fallback SHALL remain for windows too short to fit the compacted content.

#### Scenario: Done button visible on open
- **WHEN** the Heater Calibration popup is opened on a 1280×800 landscape window
- **THEN** the Done and Cancel buttons are visible without any scrolling

#### Scenario: Very short window still reachable
- **WHEN** the popup content is taller than 85% of a very short window
- **THEN** the content can be scrolled vertically so all controls, including Done, remain reachable

### Requirement: Layout change preserves existing behavior
The inline-label restructure SHALL NOT change any parameter's range, step size, display text format, backing `Settings.hardware` property, `KeyNavigation` tab order (idle temp → warmup flow → test flow → time-out → fan threshold → defaults → done), or `accessibleName`.

#### Scenario: Tab order unchanged
- **WHEN** the user presses Tab repeatedly starting from the heater idle temperature control
- **THEN** focus moves through warmup flow, test flow, time-out, fan threshold, "Defaults for cafe", then Done, exactly as before the layout change

#### Scenario: Values and formats unchanged
- **WHEN** the user adjusts any of the five controls
- **THEN** the same value ranges, step sizes, and display formats apply as before the layout change (including "Always on" at fan threshold 0)

