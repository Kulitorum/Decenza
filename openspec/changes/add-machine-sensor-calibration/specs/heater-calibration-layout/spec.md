## ADDED Requirements

### Requirement: Nominal heater voltage lives in the Heater Calibration popup
The Heater Calibration popup SHALL include a nominal heater voltage control offering 120 V and 230 V, placed after the fan threshold row and before the "Defaults for cafe" button. It SHALL show the voltage the machine reports, including an explicit unknown state with neither option preselected, and selecting a value SHALL write it to the machine and then follow the machine's readback.

It is deliberately placed here rather than on the Machine settings tab: it is behind the popup's existing destructive-change warning, among expert parameters, and away from settings a user browses casually. Nothing in the app SHALL offer a second way to change it.

#### Scenario: Voltage is reachable only through the warned popup
- **WHEN** the user looks on the Machine settings tab or anywhere else in settings for a heater voltage control
- **THEN** none is offered, and the control is reachable only inside the Heater Calibration popup, which is itself behind the calibration warning dialog

#### Scenario: Unknown voltage is shown as unknown
- **WHEN** the machine reports no nominal heater voltage
- **THEN** the control shows unknown and neither 120 V nor 230 V is preselected

#### Scenario: Setting the voltage
- **WHEN** the user selects 230 V
- **THEN** the machine receives a nominal heater voltage write of 230
- **AND** the displayed value follows the machine's readback rather than the value sent

#### Scenario: Voltage is machine state, not a preference
- **WHEN** the user presses "Defaults for cafe"
- **THEN** the five heater parameters are reset as before
- **AND** the nominal heater voltage is NOT changed

## MODIFIED Requirements

### Requirement: Parameter labels sit left of their value controls
In the Heater Calibration popup (`calibrationPopup` in `SettingsCalibrationTab.qml`), each of the six parameters (Heater idle temperature, Heater warmup flow rate, Heater test flow rate, Heater test time-out, Fan temperature threshold, Nominal heater voltage) SHALL be presented as a single horizontal row: the descriptive label on the left and its control on the right of the same row. Labels SHALL keep their existing translation keys, caption font, and per-parameter colors (temperature/flow/secondary), and SHALL word-wrap within the row when the translated text does not fit on one line.

#### Scenario: Label and value share a row
- **WHEN** the Heater Calibration popup is opened
- **THEN** each parameter's label renders on the same horizontal row as its control, label left, control right
- **AND** no parameter label renders on its own row above the control

#### Scenario: Long translation wraps instead of clipping
- **WHEN** the active language renders a parameter label wider than the space left of the control
- **THEN** the label wraps to additional lines within its row
- **AND** the control remains fully visible and right-aligned

### Requirement: Dialog fits without scrolling at typical sizes
The Heater Calibration popup SHALL be short enough that its full content — title, all six parameter rows, the "Defaults for cafe" button, and the Cancel/Done row — is visible without scrolling whenever the popup's height cap (`parent.height * 0.85`) is not exceeded by the compacted layout, which SHALL hold on the reference tablet resolution (1280×800 landscape) and typical desktop windows. A vertical scroll fallback SHALL remain for windows too short to fit the compacted content.

#### Scenario: Done button visible on open
- **WHEN** the Heater Calibration popup is opened on a 1280×800 landscape window
- **THEN** the Done and Cancel buttons are visible without any scrolling

#### Scenario: Very short window still reachable
- **WHEN** the popup content is taller than 85% of a very short window
- **THEN** the content can be scrolled vertically so all controls, including Done, remain reachable

### Requirement: Layout change preserves existing behavior
The inline-label restructure SHALL NOT change any parameter's range, step size, display text format, backing `Settings.hardware` property, or `accessibleName`. The `KeyNavigation` tab order SHALL be idle temp → warmup flow → test flow → time-out → fan threshold → nominal heater voltage → defaults → done: the voltage control is inserted into the existing chain and no other link changes.

#### Scenario: Tab order unchanged
- **WHEN** the user presses Tab repeatedly starting from the heater idle temperature control
- **THEN** focus moves through warmup flow, test flow, time-out, fan threshold, nominal heater voltage, "Defaults for cafe", then Done
- **AND** every link other than the inserted voltage step is exactly as it was before

#### Scenario: Values and formats unchanged
- **WHEN** the user adjusts any of the five original controls
- **THEN** the same value ranges, step sizes, and display formats apply as before the layout change (including "Always on" at fan threshold 0)
