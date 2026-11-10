# Spec Delta

## ADDED Requirements

### Requirement: Each pitcher preset has its own steam rate

Every pitcher preset SHALL store its own steam rate in seconds per gram of milk, and weight scaling SHALL use the selected preset's rate. A rate of 0 SHALL mean that preset is uncalibrated. The user SHALL be able to adjust the selected preset's rate directly. The empty-pitcher tare SHALL be the per-preset `pitcherWeightG`, and net milk SHALL require a saved pitcher weight (one consistent rule shared with auto-capture).

#### Scenario: Two presets, two rates
- **WHEN** "Whole milk" has a rate of 0.14 s/g and "Oat" has 0.18 s/g, and 200 g is captured against each in turn
- **THEN** "Whole milk" steams for 28 s and "Oat" for 36 s

#### Scenario: Editing one preset's rate leaves the others alone
- **WHEN** "Oat" is selected and the user changes the steam rate from 0.18 to 0.20 s/g
- **THEN** "Oat" stores 0.20 s/g and "Whole milk" still stores 0.14 s/g

#### Scenario: Rate shown follows the selected preset
- **WHEN** the Steam page is open and the user selects a different pitcher
- **THEN** the steam rate control shows that pitcher's rate

#### Scenario: Same pitcher, two fluids
- **WHEN** two presets share the same saved empty-pitcher weight but have different rates
- **THEN** each scales steam time at its own rate, subtracting the same pitcher weight

#### Scenario: No pitcher weight saved
- **WHEN** the selected pitcher has no saved empty-pitcher weight
- **THEN** net milk resolves to 0 and steaming falls back to the fixed duration

#### Scenario: Recipe selects its pitcher
- **WHEN** a recipe naming "Oat" is activated
- **THEN** "Oat" is selected and its own rate scales the steam time

### Requirement: An existing steam rate carries over to every pitcher

When the app first runs with per-preset rates, and when a backup made before them is imported, every pitcher preset without a rate of its own SHALL take the previous global rate. A preset that already has its own rate SHALL keep it.

#### Scenario: Upgrade keeps steam times
- **WHEN** the global rate was 0.14 s/g with pitchers "Small" and "Large", and the app is upgraded
- **THEN** both pitchers have a rate of 0.14 s/g and scale steam time exactly as before

#### Scenario: Upgrade with no calibration
- **WHEN** the global rate was never calibrated and the app is upgraded
- **THEN** every pitcher is uncalibrated and steams for its fixed duration

#### Scenario: Older backup imported
- **WHEN** a backup holding a global rate of 0.16 s/g and presets without rates is imported
- **THEN** each imported preset has a rate of 0.16 s/g

#### Scenario: Recipe recreates a deleted pitcher
- **WHEN** a recipe saved with "Oat" at 0.18 s/g is activated after "Oat" was deleted
- **THEN** "Oat" is recreated with a rate of 0.18 s/g

#### Scenario: Seeding does not overwrite
- **WHEN** "Oat" already stores 0.18 s/g and a seed from the old global rate of 0.14 s/g runs
- **THEN** "Oat" keeps 0.18 s/g

### Requirement: A new pitcher starts with the selected pitcher's rate

A pitcher preset created from the Steam page SHALL start with the rate of the preset selected at that moment, or uncalibrated when Heater off is selected. A preset created through another surface that states a rate SHALL use the stated rate.

#### Scenario: Second pitcher for the same fluid
- **WHEN** "Small" (0.14 s/g) is selected and the user adds a pitcher named "Large"
- **THEN** "Large" starts at 0.14 s/g

#### Scenario: Added while Heater off is selected
- **WHEN** Heater off is selected and the user adds a pitcher
- **THEN** the new pitcher is uncalibrated

## MODIFIED Requirements

### Requirement: Steam time scales to measured milk weight

When weight-timed steaming is on, the selected pitcher's steam rate is calibrated, and milk is measured, the system SHALL set the steam timeout to `clamp(round(rate × milk), 5, 120)` seconds, where `rate` is the selected pitcher preset's seconds-per-gram rate. The math SHALL live in one place (`SettingsBrew::scaledSteamTime`) and be reused by every caller.

#### Scenario: Full pitcher scales up from the reference
- **WHEN** the selected pitcher's rate is 0.14 s/g and `500 g` of milk is measured
- **THEN** the steam timeout is set to `70 s`

#### Scenario: Half pitcher scales down from the reference
- **WHEN** the selected pitcher's rate is 0.14 s/g and `125 g` of milk is measured
- **THEN** the steam timeout is set to `18 s` (rounded)

#### Scenario: Scaled time is clamped to a safe range
- **WHEN** the scaled result would fall below 5 s or above 120 s
- **THEN** the steam timeout is clamped to 5 s or 120 s respectively

### Requirement: Single on/off toggle, off by default, that preserves the calibration

The system SHALL provide one user-facing toggle ("Weight-timed steaming"), **off by default**, that enables or disables weight scaling for every pitcher without discarding any pitcher's stored rate. When off, every path (pill tap, live click, steam start) SHALL use the preset's fixed `duration` and auto-capture SHALL not run. Calibrating from a steam pour SHALL turn the toggle on.

#### Scenario: Off by default
- **WHEN** a user has never enabled the feature
- **THEN** steaming uses the preset's fixed `duration` and no weight scaling occurs

#### Scenario: Calibrating turns it on
- **WHEN** the user calibrates from the last steam while the toggle is off
- **THEN** the toggle is enabled so scaling takes effect

#### Scenario: Toggle off retains the calibration
- **WHEN** pitchers have rates set and the user turns the toggle off
- **THEN** steaming uses the fixed `duration`, and every pitcher's stored rate is unchanged

#### Scenario: Toggle back on resumes scaling
- **WHEN** the toggle was off with saved rates and the user turns it on
- **THEN** weight scaling resumes from each pitcher's stored rate with no re-calibration

### Requirement: Disabled or uncalibrated steaming preserves fixed-duration behavior

When the toggle is off, or the selected pitcher's steam rate is uncalibrated (0), the system SHALL use the preset's fixed `duration`, exactly as before this feature. A 0 or missing `duration` SHALL also fall back rather than producing a silently wrong scaled time. Milk SHALL still be captured for an uncalibrated pitcher, so its pour can calibrate it.

#### Scenario: Uncalibrated pitcher
- **WHEN** the toggle is on but the selected pitcher's rate is 0
- **THEN** the steam timeout is the preset's fixed `duration`

#### Scenario: One pitcher calibrated, another not
- **WHEN** "Small" has a rate of 0.14 s/g, "Large" has a rate of 0, and 200 g of milk is captured against "Large"
- **THEN** the steam timeout is "Large"'s fixed `duration`

#### Scenario: Calibrated but no milk measured this session
- **WHEN** scaling is active but no milk is on the scale and none was captured this session
- **THEN** the steam timeout falls back to the preset's fixed `duration`

#### Scenario: Uncalibrated pitcher is still weighed
- **WHEN** the toggle is on, the selected pitcher has a saved empty weight and a rate of 0, and milk settles on the scale during a requested sample
- **THEN** the milk is captured and recorded for the session, and the steam time stays the fixed `duration`

### Requirement: Calibrate from an actual steam pour

The system SHALL record the actual elapsed steam time, that session's measured milk weight and the pitcher preset it was steamed with as one record at session end, from any screen. A single explicit action SHALL set that pitcher's steam rate from the record (time ÷ milk), offered only while that pitcher is selected; calibration SHALL NOT happen automatically on every pour. The affordance SHALL display the most recent pour as `Last steam: <milk> g milk → <time> s`.

#### Scenario: Elapsed time captured at session end
- **WHEN** a steam session that had a captured milk weight ends
- **THEN** the actual elapsed time, that milk weight and the selected pitcher are saved as the last pour, regardless of which screen started steaming

#### Scenario: One-tap adopt as baseline
- **WHEN** the last pour was `250 g → 35 s` with "Oat", "Oat" is selected, and the user taps "Use last steam"
- **THEN** "Oat"'s steam rate becomes `0.14 s/g` and no other pitcher's rate changes

#### Scenario: Last pour was a different pitcher
- **WHEN** the last pour was steamed with "Oat" and "Whole milk" is selected
- **THEN** "Use last steam" is not available, and the text says the last steam was with "Oat"

#### Scenario: Pitcher renamed since the pour
- **WHEN** the pitcher the last pour was steamed with is renamed and is selected
- **THEN** "Use last steam" is still available for it, and for no pitcher that later takes the old name

#### Scenario: Pitcher deleted since the pour
- **WHEN** the pitcher the last pour was steamed with is deleted
- **THEN** "Use last steam" is not available for any pitcher until the next weighed steam

#### Scenario: Two pitchers share a name
- **WHEN** a weighed steam ends with a pitcher whose name another stored pitcher also has
- **THEN** no pitcher is recorded with the pour, and "Use last steam" is not available for either

#### Scenario: A recipe is activated after the pour
- **WHEN** a pour of `250 g → 35 s` was recorded with "Oat" and a recipe with a planned milk weight of 180 g is then activated
- **THEN** "Use last steam" still shows `250 g → 35 s` and calibrates "Oat" from it

#### Scenario: A measured rate is stored as measured
- **WHEN** the last pour was `30 g → 70 s` and the user taps "Use last steam"
- **THEN** the pitcher's rate is `2.33 s/g`, not a rounded-down limit

#### Scenario: Last pour recorded before pitchers were remembered
- **WHEN** the stored last pour has no pitcher recorded
- **THEN** "Use last steam" is not available until the next weighed steam

### Requirement: Fallback-to-fixed-duration is diagnosable from the debug log

When weight-timed steaming applies the fixed duration instead of a weight-scaled one, the debug log SHALL show why: the captured milk, the toggle, the selected pitcher and that pitcher's steam rate, the computed and the applied duration. It SHALL cover the steam-start decision and the page-activation/pitcher-lift sync, whether steam was started from the app or the group head.

#### Scenario: Fallback due to missing calibration is visible in the log
- **WHEN** steaming starts with milk captured but the selected pitcher's seconds-per-gram rate is uncalibrated
- **THEN** the debug log shows the captured milk value, the toggle as enabled, the pitcher and its calibration value as unset/zero, and the applied duration as the fixed fallback

#### Scenario: Fallback due to no captured milk is distinguishable from a calibration gap
- **WHEN** steaming starts with no milk captured
- **THEN** the debug log says no milk was captured, distinguishing this case from an uncalibrated-but-milk-present fallback

#### Scenario: Successful scaling is also logged
- **WHEN** steaming starts with milk captured, the toggle enabled, a valid calibration, and an enabled pitcher
- **THEN** the debug log shows the computed scaled duration and records it as the applied source, not the fallback

## REMOVED Requirements

### Requirement: One global steam rate

**Reason**: One rate cannot serve presets used for different fluids or different target temperatures, and presets already differ in steam flow and heater temperature.

**Migration**: Each existing pitcher preset is seeded with the old global rate ("An existing steam rate carries over to every pitcher"). The per-preset tare rule moves to "Each pitcher preset has its own steam rate". Clients that read or set the global rate use the selected preset's rate instead.
