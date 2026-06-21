## ADDED Requirements

### Requirement: Steam time scales to measured milk weight

When weight-timed steaming is on, the selected pitcher is calibrated, and milk is on a connected scale, the system SHALL set the steam timeout to the calibrated rate applied to the measured milk weight, so the DE1 auto-stop lands at the calibrated temperature regardless of milk quantity.

The scaled time SHALL be computed as `clamp(round(duration × measuredMilk / referenceMilk), 5, 120)` seconds, where `measuredMilk = scaleReading − pitcherTare`. The scaling math SHALL be defined in exactly one place (`SettingsBrew::scaledSteamTime`) and reused by every caller.

#### Scenario: Full pitcher scales up from the reference
- **WHEN** the calibration is `250 g → 35 s` and `500 g` of milk is measured
- **THEN** the steam timeout is set to `70 s`

#### Scenario: Half pitcher scales down from the reference
- **WHEN** the calibration is `250 g → 35 s` and `125 g` of milk is measured
- **THEN** the steam timeout is set to `18 s` (rounded)

#### Scenario: Scaled time is clamped to a safe range
- **WHEN** the scaled result would fall below 5 s or above 120 s
- **THEN** the steam timeout is clamped to 5 s or 120 s respectively

### Requirement: Per-pitcher calibration

Each steam pitcher preset SHALL carry its own reference milk weight (`calibMilkG`) paired with its `duration`. Calibrations SHALL be independent per pitcher, so different drinks or temperature preferences can use different pitchers. The empty-pitcher tare SHALL be the existing per-preset `pitcherWeightG`; net milk SHALL require a saved pitcher weight (one consistent rule shared with auto-capture).

#### Scenario: Switching pitchers uses that pitcher's reference
- **WHEN** two pitchers have different reference milk → duration pairs and the user switches between them
- **THEN** each scales steam time from its own calibration

#### Scenario: No pitcher weight saved
- **WHEN** the selected pitcher has no saved empty-pitcher weight
- **THEN** net milk resolves to 0 and steaming falls back to the fixed duration

### Requirement: Single on/off toggle, off by default, that preserves the calibration

The system SHALL provide one user-facing toggle ("Weight-timed steaming"), **off by default**, that enables or disables weight scaling without discarding the stored calibration. When off, the system SHALL use the preset's fixed `duration` on every path (pill-tap, live-click, steam-start), and auto-capture SHALL not run. Setting a pitcher's reference milk SHALL turn the toggle on automatically (the explicit opt-in). The per-pitcher calibration SHALL be retained so turning the toggle back on resumes scaling with no re-calibration.

#### Scenario: Off by default
- **WHEN** a user has never enabled the feature
- **THEN** steaming uses the preset's fixed `duration` and no weight scaling occurs

#### Scenario: Calibrating turns it on
- **WHEN** the user sets a pitcher's reference milk while the toggle is off
- **THEN** the toggle is enabled so scaling takes effect for that pitcher

#### Scenario: Toggle off retains the calibration
- **WHEN** a calibration is set and the user turns the toggle off
- **THEN** steaming uses the fixed `duration`, and the stored reference values are unchanged

#### Scenario: Toggle back on resumes scaling
- **WHEN** the toggle was off with a saved calibration and the user turns it on
- **THEN** weight scaling resumes from the stored reference with no re-calibration

### Requirement: Disabled or uncalibrated steaming preserves fixed-duration behavior

When the toggle is off, or the selected pitcher is uncalibrated, the system SHALL use the preset's fixed `duration`, exactly as before this feature. A 0 or missing `duration` SHALL also fall back rather than producing a silently wrong scaled time.

#### Scenario: Uncalibrated pitcher
- **WHEN** the toggle is on but the selected pitcher has no reference milk
- **THEN** the steam timeout is the preset's fixed `duration`

#### Scenario: Calibrated but no milk measured this session
- **WHEN** scaling is active but no milk is on the scale and none was captured this session
- **THEN** the steam timeout falls back to the preset's fixed `duration`

### Requirement: Calibrate from an actual steam pour

The system SHALL record the actual elapsed steam time and that session's measured milk weight as an atomic pair at session end, from any screen. A single explicit action SHALL adopt that pair as the selected pitcher's reference; calibration SHALL NOT happen automatically on every pour. The affordance SHALL display the most recent pour as `Last: <milk> g → <time> s`.

#### Scenario: Elapsed time captured at session end
- **WHEN** a steam session that had a captured milk weight ends
- **THEN** the actual elapsed time and that milk weight are saved as the last pour, regardless of which screen started steaming

#### Scenario: One-tap adopt as baseline
- **WHEN** the user taps "Use as baseline" with a last pour of `250 g → 35 s`
- **THEN** the selected pitcher's reference becomes `250 g → 35 s`

### Requirement: Automatic milk capture before steaming

While preparing to steam, the system SHALL detect the settled milk weight using the shared virtual-zero stable-weight capture (robust to an un-zeroed scale), give the standard capture confirmation (ding + transient banner), and lock the scaled time. Auto-capture SHALL be gated to the active page so it cannot fire twice when the home screen and the steam page are both loaded.

#### Scenario: Milk settles and the time locks
- **WHEN** the pitcher with milk holds steady within tolerance for the dwell
- **THEN** the steam timeout is set to the scaled value, a ding + banner are shown, and the milk is recorded for this session

#### Scenario: No double capture across pages
- **WHEN** the steam page is opened over the home screen while steam mode is active
- **THEN** only the active page's capture fires (no double ding / double write)

### Requirement: Scaled time applies even when the pitcher never leaves the scale

Because the virtual-zero detector seeds to pitcher+milk when the loaded pitcher rests on the scale the whole time (so the settle capture never fires), the system SHALL also apply the scaled time at steam-start from the last on-scale / session-captured milk, pushing it to the DE1 so it takes effect for that session. The scaled value SHALL survive lifting the pitcher to the wand and the home→steam page change.

#### Scenario: Pitcher rests on the scale the whole time
- **WHEN** the loaded pitcher never leaves the scale and the user starts steaming
- **THEN** the steam time is scaled from the on-scale milk at steam-start, not left at the baseline

#### Scenario: Home-screen capture survives the page change
- **WHEN** milk is captured in the home-screen steam flow and the live steam page then activates
- **THEN** the scaled time is preserved rather than reset to the baseline duration

### Requirement: Manual timer override is preserved against auto-scaling

When the user adjusts the steam timer by hand (±5), the system SHALL keep that value for the current session — auto-capture and steam-start SHALL NOT overwrite it. The override SHALL clear when the pitcher leaves the scale, the pitcher selection changes, or the session ends, so a fresh pour re-arms scaling. The mechanism SHALL be event-based (no timers).

#### Scenario: ±5 after a capture is kept
- **WHEN** the auto-capture has set a scaled time and the user then taps ±5
- **THEN** the adjusted time is used and is not overwritten by re-scaling for that session

#### Scenario: Override re-arms on a fresh pour
- **WHEN** the user nudged ±5 and then lifts the pitcher off the scale
- **THEN** the next placement re-enables weight scaling

### Requirement: Live expected-time feedback

When scaling is active and milk is on the scale during steam setup, the system SHALL show the expected steam time for the milk currently on the scale before steaming begins.

#### Scenario: Expected time preview
- **WHEN** scaling is active and milk is resting on the scale on the steam setup view
- **THEN** the UI shows the computed expected steam time for that milk weight
