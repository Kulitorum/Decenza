## MODIFIED Requirements

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

## ADDED Requirements

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

## REMOVED Requirements

### Requirement: Scaled time applies even when the pitcher never leaves the scale

**Reason**: It scaled the steam from whatever rested on the scale at steam start, which counts a cup as milk. A pitcher resting on the scale is captured as soon as a sample is offered.
**Migration**: Open the idle pitchers or tap the review-page milk button with the pitcher on the scale.

### Requirement: Live expected-time feedback

**Reason**: The steam page previewed a time for whatever was on the scale, which is not what the steam uses; only a capture changes the time.
**Migration**: The steam plan line shows the time from the captured milk.
