# weight-timed-steaming Specification

## Purpose
Scales the DE1's steam auto-stop duration to the measured milk weight on a connected scale using one calibrated seconds-per-gram steam rate, so a full or partial pitcher steams to the same target temperature. Covers the off-by-default toggle, calibrating from an actual pour, pre-steam milk capture (idle screen, steam page and shot review page), manual timer override precedence, and the on-scale-the-whole-time and live-preview edge cases.

## Requirements

### Requirement: Steam time scales to measured milk weight

When weight-timed steaming is on, the steam rate is calibrated, and milk is measured, the system SHALL set the steam timeout to `clamp(round(rate × milk), 5, 120)` seconds, where `rate` is the global seconds-per-gram rate. The math SHALL live in one place (`SettingsBrew::scaledSteamTime`) and be reused by every caller.

#### Scenario: Full pitcher scales up from the reference
- **WHEN** the rate is 0.14 s/g and `500 g` of milk is measured
- **THEN** the steam timeout is set to `70 s`

#### Scenario: Half pitcher scales down from the reference
- **WHEN** the rate is 0.14 s/g and `125 g` of milk is measured
- **THEN** the steam timeout is set to `18 s` (rounded)

#### Scenario: Scaled time is clamped to a safe range
- **WHEN** the scaled result would fall below 5 s or above 120 s
- **THEN** the steam timeout is clamped to 5 s or 120 s respectively

### Requirement: Single on/off toggle, off by default, that preserves the calibration

The system SHALL provide one user-facing toggle ("Weight-timed steaming"), **off by default**, that enables or disables weight scaling without discarding the stored rate. When off, every path (pill tap, live click, steam start) SHALL use the preset's fixed `duration` and auto-capture SHALL not run. Calibrating from a steam pour SHALL turn the toggle on.

#### Scenario: Off by default
- **WHEN** a user has never enabled the feature
- **THEN** steaming uses the preset's fixed `duration` and no weight scaling occurs

#### Scenario: Calibrating turns it on
- **WHEN** the user calibrates from the last steam while the toggle is off
- **THEN** the toggle is enabled so scaling takes effect

#### Scenario: Toggle off retains the calibration
- **WHEN** a rate is set and the user turns the toggle off
- **THEN** steaming uses the fixed `duration`, and the stored rate is unchanged

#### Scenario: Toggle back on resumes scaling
- **WHEN** the toggle was off with a saved rate and the user turns it on
- **THEN** weight scaling resumes from the stored rate with no re-calibration

### Requirement: Disabled or uncalibrated steaming preserves fixed-duration behavior

When the toggle is off, or the steam rate is uncalibrated (0), the system SHALL use the preset's fixed `duration`, exactly as before this feature. A 0 or missing `duration` SHALL also fall back rather than producing a silently wrong scaled time.

#### Scenario: Uncalibrated pitcher
- **WHEN** the toggle is on but the steam rate is 0
- **THEN** the steam timeout is the preset's fixed `duration`

#### Scenario: Calibrated but no milk measured this session
- **WHEN** scaling is active but no milk is on the scale and none was captured this session
- **THEN** the steam timeout falls back to the preset's fixed `duration`

### Requirement: Calibrate from an actual steam pour

The system SHALL record the actual elapsed steam time and that session's measured milk weight as an atomic pair at session end, from any screen. A single explicit action SHALL set the global steam rate from that pair (time ÷ milk); calibration SHALL NOT happen automatically on every pour. The affordance SHALL display the most recent pour as `Last steam: <milk> g milk → <time> s`.

#### Scenario: Elapsed time captured at session end
- **WHEN** a steam session that had a captured milk weight ends
- **THEN** the actual elapsed time and that milk weight are saved as the last pour, regardless of which screen started steaming

#### Scenario: One-tap adopt as baseline
- **WHEN** the user taps "Use last steam" with a last pour of `250 g → 35 s`
- **THEN** the steam rate becomes `0.14 s/g`

### Requirement: Automatic milk capture before steaming

The system SHALL capture milk only for a sample the user requests, from the review-page milk button or the idle-screen steam pitchers (Steam row or compact Steam popup). The pitcher SHALL be weight-timed with a saved empty weight. Each request SHALL collect one sample, confirm it and lock the scaled time. It SHALL end on capture, when its surface closes, or when any operation other than a wand purge starts. Steam mode, the steam page and a finished steam SHALL NOT start one.

#### Scenario: Milk settles and the time locks
- **WHEN** the user opens the idle pitchers and the pitcher with milk holds steady within tolerance for the dwell
- **THEN** the steam timeout is set to the scaled value, a ding + banner are shown, and the milk is recorded for this session

#### Scenario: No double capture across pages
- **WHEN** the steam page is opened over the home screen while steam mode is active
- **THEN** the idle page's request ends and nothing is captured on the steam page

#### Scenario: Cup after a steam is not taken for milk
- **WHEN** a steam has just ended, no sample is requested, and the user sets an espresso cup on the scale
- **THEN** nothing is captured and the steam time is unchanged

#### Scenario: One sample per request
- **WHEN** milk was captured from the idle pitchers and the user then sets a different load on the scale with the pitchers still showing
- **THEN** nothing further is captured until the pitchers are opened again

#### Scenario: Steam start ends the request
- **WHEN** a sample is requested and steam starts from the group head before anything is captured
- **THEN** the request ends as the steam starts, and the steam uses the earlier capture or the preset time

#### Scenario: Pitcher without an empty weight
- **WHEN** the user opens the idle pitchers with a pitcher selected that has no saved empty weight
- **THEN** nothing is captured

#### Scenario: The bar Steam popup behaves like the Home row
- **WHEN** the Steam widget sits in a bar, the user opens its pitcher popup and taps a different pitcher
- **THEN** the popup stays open with that pitcher selected and its sample is captured; only tapping the selected pitcher (start steam) or Purge closes it

#### Scenario: Closing the pitchers ends the request
- **WHEN** the user opens the idle pitchers and closes them before a sample is captured
- **THEN** the request ends and the session's milk weight is what it was before

### Requirement: Manual timer override is preserved against auto-scaling

When the user adjusts the steam timer by hand (±5), the system SHALL keep that value for the current session — auto-capture and steam-start SHALL NOT overwrite it. The override SHALL clear when the pitcher leaves the scale, the pitcher selection changes, or the session ends, so a fresh pour re-arms scaling. The mechanism SHALL be event-based (no timers).

#### Scenario: ±5 after a capture is kept
- **WHEN** the auto-capture has set a scaled time and the user then taps ±5
- **THEN** the adjusted time is used and is not overwritten by re-scaling for that session

#### Scenario: Override re-arms on a fresh pour
- **WHEN** the user nudged ±5 and then lifts the pitcher off the scale
- **THEN** the next placement re-enables weight scaling

### Requirement: Idle-page pitcher placement prompt

The idle page SHALL show a "Place the milk pitcher on the scale" prompt below the steam pitcher pills only while a milk sample is requested from those pills, the selected pitcher is weight-timed with a saved empty weight, the idle page is active, a real (non-flow) scale is connected, and nothing is on the scale. Its second line SHALL mention the beep only when the capture sound is on; otherwise it SHALL ask the user to hold the pitcher until the weight registers.

#### Scenario: Prompt hidden when weight-timed steaming is off

- **WHEN** weight-timed steaming is disabled and steam is selected with a connected scale
- **THEN** no pitcher-placement prompt is shown

#### Scenario: Prompt hidden after the sample is taken

- **WHEN** milk was captured from the idle pitchers and the pitcher is lifted
- **THEN** no pitcher-placement prompt is shown

#### Scenario: Beep wording only when the sound is enabled

- **WHEN** weight-timed steaming is on and the capture sound option is off
- **THEN** the prompt asks the user to hold the pitcher until the weight registers, without mentioning a beep

#### Scenario: Prompt clears when the pitcher is placed

- **WHEN** the user places a pitcher (load above the detection threshold) on the scale
- **THEN** the prompt disappears

### Requirement: Fallback-to-fixed-duration is diagnosable from the debug log

When weight-timed steaming applies the fixed duration instead of a weight-scaled one, the debug log SHALL show why: the captured milk, the toggle, the steam rate, the selected pitcher, the computed and the applied duration. It SHALL cover the steam-start decision and the page-activation/pitcher-lift sync, whether steam was started from the app or the group head.

#### Scenario: Fallback due to missing calibration is visible in the log
- **WHEN** steaming starts with milk captured but the global steam-seconds-per-gram rate is uncalibrated
- **THEN** the debug log shows the captured milk value, the toggle as enabled, the calibration value as unset/zero, and the applied duration as the fixed fallback

#### Scenario: Fallback due to no captured milk is distinguishable from a calibration gap
- **WHEN** steaming starts with no milk captured
- **THEN** the debug log says no milk was captured, distinguishing this case from an uncalibrated-but-milk-present fallback

#### Scenario: Successful scaling is also logged
- **WHEN** steaming starts with milk captured, the toggle enabled, a valid calibration, and an enabled pitcher
- **THEN** the debug log shows the computed scaled duration and records it as the applied source, not the fallback

### Requirement: Review-page milk button

The shot page SHALL offer a milk-weigh button immediately left of the Read TDS button, shown only while weight-timed steaming is on, at least one usable pitcher exists, a real (non-flow) scale is connected, and the page shows the most recently saved shot. A usable pitcher is an enabled pitcher preset with a saved empty-pitcher weight. At rest the button SHALL show the selected pitcher's name, followed by the captured milk weight once there is one.

#### Scenario: Button hidden without the prerequisites
- **WHEN** weight-timed steaming is off, or no usable pitcher exists, or no real scale is connected
- **THEN** the milk-weigh button is not shown

#### Scenario: Selected pitcher is not usable
- **WHEN** Heater off is the selected pitcher and the user taps the button
- **THEN** the button, which shows "Choose pitcher", selects the next usable pitcher and shows its name, with no capture started

#### Scenario: Captured weight shown
- **WHEN** 180 g of milk has been captured against "Small"
- **THEN** the button shows "Small · 180 g"

#### Scenario: An older shot opened from history
- **WHEN** the user opens a shot from history that is not the most recently saved one
- **THEN** the milk-weigh button is not shown, since no steam follows that shot

### Requirement: Review-page capture is the shared capture attempt

Tapping the milk-weigh button SHALL start the same capture attempt as opening the steam pitchers on the idle screen, using the one capture implementation shared with the idle screen. Net milk SHALL be the settled reading minus the settled empty reading minus the pitcher's saved weight. A capture SHALL record the session's milk weight and set and send the scaled steam time.

#### Scenario: Pour off the scale, then place
- **WHEN** the user taps the button and sets down a pitcher holding 180 g of milk
- **THEN** 180 g of milk is captured

#### Scenario: Pour into the pitcher on the scale
- **WHEN** the user taps the button, sets down the empty pitcher, and pours milk into it
- **THEN** nothing is captured while only the empty pitcher is on the scale, and the milk is captured once the pour stops and the reading settles

#### Scenario: Group-head steam uses the captured weight
- **WHEN** milk was captured from the review page and the user starts steam from the group head
- **THEN** the steam time is scaled to the captured milk weight

### Requirement: Capture attempts do not tare

Starting a capture attempt SHALL NOT tare the scale, whether it starts on the idle screen, in the compact steam widget, or from the review-page button. Every net-milk value read from the scale SHALL be measured above the learned empty reading when there is one.

#### Scenario: Cup or pitcher still on the scale at the tap
- **WHEN** the user taps the button with the cup or pitcher on the scale, then lifts it and sets down the pitcher with milk
- **THEN** no tare is sent and the captured weight is the milk only

#### Scenario: Scale not reading zero when empty
- **WHEN** the empty scale reads a few grams above zero and the user taps the button
- **THEN** the capture still starts and waits for the pitcher

#### Scenario: Milk readouts use the capture's empty reading
- **WHEN** after a shot the empty scale reads −120 g, Steam is selected on idle, and a 25 g pitcher with 150 g of milk is set down
- **THEN** the pitcher pill and a Weight widget in Net milk mode show 150 g

#### Scenario: Pitcher set down before the attempt starts
- **WHEN** the empty scale reads −450 g, the user sets down a 50 g pitcher holding 100 g of milk, and then selects Steam on idle or taps the review-page milk button
- **THEN** 100 g of milk is captured once the reading has settled

#### Scenario: Steam started before the capture settles
- **WHEN** the empty scale reads −450 g and the user starts steam within the settling time of setting down a 50 g pitcher holding 100 g of milk
- **THEN** the steam time is scaled to 100 g of milk

### Requirement: Review-page capture states

While a capture is in progress the button SHALL show "Place pitcher" until a load is on the scale and "Weighing…" while it settles. A settled load that is not captured SHALL be explained, here and on the idle screen: "Add milk" under 50 g, "Not <pitcher>?" when lighter than the empty pitcher. Tapping the button again SHALL select the next usable pitcher, show its name for about two seconds, and keep the capture in progress. Leaving the page SHALL end the capture; milk captured before it stands.

#### Scenario: Too little milk to capture
- **WHEN** the pitcher settles on the scale with less than 50 g of milk
- **THEN** the button shows "Add milk", and once milk is added and the reading settles above 50 g it captures

#### Scenario: Load lighter than the selected pitcher
- **WHEN** "Small" (saved 145 g) is selected and a 25 g pitcher with 80 g of milk settles on the scale
- **THEN** the button shows "Not Small?" rather than "Add milk"

#### Scenario: Tapping again cycles the pitcher
- **WHEN** the user taps the button, "Small" and "Large" are usable, and the user taps it again before a capture
- **THEN** "Large" is selected, the button shows "Large" for about two seconds and then "Place pitcher" again, and the capture stays in progress for "Large"

#### Scenario: Cancel keeps the earlier capture
- **WHEN** 180 g was captured on the idle screen, and on the review page the user taps the button and then leaves the page
- **THEN** the capture stops and the session's milk weight stays 180 g

### Requirement: Pitcher selection from the review-page milk button

Long-pressing the milk-weigh button SHALL select the next usable pitcher, wrapping around. The selection SHALL behave like selecting a pitcher anywhere else in the app (an active recipe deactivates and captured milk clears). A capture in progress SHALL continue for the newly selected pitcher. Screen-reader users SHALL be able to do the same through an accessibility action, and the accessible name SHALL include the pitcher's name.

#### Scenario: Long-press cycles to the next pitcher
- **WHEN** pitchers "Small", "Heater off" and "Large" exist, "Small" is selected, and the user long-presses the button
- **THEN** "Large" becomes the selected pitcher and the button shows "Large"

#### Scenario: Cycling clears the captured weight
- **WHEN** milk was captured against "Small" and the user long-presses to select "Large"
- **THEN** the captured weight is cleared and the button shows "Large" without a weight

#### Scenario: Screen reader cycles pitchers
- **WHEN** a screen-reader user invokes the button's next-pitcher action
- **THEN** the next usable pitcher is selected and its name is announced

### Requirement: One global steam rate

Weight scaling SHALL use a single steam rate in seconds per gram of milk, shared by every pitcher. Calibration SHALL set this rate, and the user SHALL also be able to adjust it directly. A rate of 0 SHALL mean uncalibrated. The empty-pitcher tare SHALL be the per-preset `pitcherWeightG`, and net milk SHALL require a saved pitcher weight (one consistent rule shared with auto-capture).

#### Scenario: Switching pitchers keeps the same rate
- **WHEN** the rate is 0.14 s/g and the user switches from one pitcher to another
- **THEN** both pitchers scale steam time at 0.14 s/g, subtracting each pitcher's own saved weight

#### Scenario: No pitcher weight saved
- **WHEN** the selected pitcher has no saved empty-pitcher weight
- **THEN** net milk resolves to 0 and steaming falls back to the fixed duration

### Requirement: The empty scale reading is learned while a scale is connected
The empty reading SHALL be learned whenever a weighing scale is connected, including while no capture attempt is open, so a cup or pitcher already on the scale when an attempt starts is measured from it.

#### Scenario: Reading learned with no attempt open
- **WHEN** a weighing scale is connected, no capture attempt is open, and the empty scale settles at −450 g
- **THEN** −450 g becomes the empty reading that a later capture attempt measures from

### Requirement: A tare, scale switch or disconnect discards the empty reading
A tare, a scale switch or a disconnect SHALL discard the learned empty reading. Until a new one is learned, a cup or pitcher on the scale is handled by lifting it, after which the lower settled reading becomes the empty reference.

#### Scenario: Pitcher lifted after a tare
- **WHEN** the scale is tared with a pitcher on it, and the user then lifts the pitcher
- **THEN** the learned reading is discarded, and the lower settled reading with the pitcher lifted becomes the empty reference

### Requirement: Steam time changes only on a capture

The planned and applied steam time SHALL be scaled only from captured milk. A load on the scale that was not captured (a pitcher lifted before it settled, or a cup on the scale at steam start) SHALL NOT change it. Offering or withdrawing a sample SHALL keep the earlier capture; selecting a different pitcher SHALL clear it. A capture SHALL survive lifting the pitcher and the change to the steam page, whichever screen captured it.

#### Scenario: Plan holds while a pitcher settles
- **WHEN** milk was captured earlier and the user places another pitcher of milk on the scale with a sample offered
- **THEN** the plan line keeps the earlier milk and time until the new capture lands

#### Scenario: Cup on the scale at steam start
- **WHEN** no milk was captured and steam starts with a cup resting on the scale
- **THEN** the steam uses the preset time

#### Scenario: Review-page capture survives the page change
- **WHEN** milk is captured on the post-shot review page and the steam page then opens before steaming starts
- **THEN** the steam page shows and sends the time scaled from that milk, not the pitcher's base duration

### Requirement: A capture does not survive a restart

Neither the captured milk nor the steam time scaled from it SHALL survive an app restart. After a restart the steam time SHALL be the selected pitcher's own.

#### Scenario: Restart returns to the pitcher's time
- **WHEN** milk was captured, scaling "Small" (65 s) to 25 s, and the app restarts before steaming
- **THEN** no milk is captured and the steam time is 65 s

### Requirement: A request follows the surface that made it

Selecting a different pitcher from the idle pitchers SHALL request a new sample. Leaving the idle page SHALL close the idle pitchers; a steam-wand purge, which stays on the idle page, SHALL keep them open and their request standing. The host SHALL accept a request only from a connected client, so closing that client always ends it.

#### Scenario: A different pitcher is weighed again
- **WHEN** milk was captured from the idle pitchers and the user taps a different pitcher with the pitchers still showing
- **THEN** the earlier capture is cleared and the milk on the scale is captured against the new pitcher

#### Scenario: Leaving the idle page closes the pitchers
- **WHEN** the idle pitchers are showing and the user leaves the idle page
- **THEN** the pitchers are closed when the user returns, and nothing is captured until they are opened again

#### Scenario: A purge keeps the request
- **WHEN** a sample is requested from the idle Steam row and the user taps Purge
- **THEN** the row stays open, and the milk set on the scale after the purge is captured
