## ADDED Requirements

### Requirement: Cup Fill Persists After Extraction Ends

The cup fill view SHALL continue to render the liquid fill, crema and its weight text after the machine leaves the espresso cycle, for as long as the view exists, provided extraction was observed on that view instance. The rendered fill SHALL be drawn from a held weight rather than the live scale reading, and the view's own weight text SHALL show that same held value so the number and the fill agree.

The held weight SHALL be the peak weight observed since flow began, so that it keeps pace with drip landing after the stop — which the saved yield also includes — and never falls when the cup is lifted off the scale.

The held state SHALL be cleared when a new espresso cycle begins on the same view instance, so a subsequent shot starts from an empty cup.

The view SHALL NOT resume its animation timer for the hold; nothing animates while the cup is held, though the fill may still redraw when late drip raises the held weight.

#### Scenario: Completed shot holds its cup until navigation

- **WHEN** an espresso finishes and the machine phase leaves the espresso cycle (to `Ready`, `Idle` or `Heating`) while the espresso page is still displayed during the post-shot stop overlay
- **THEN** the cup continues to show the coffee and crema at the fill level reached at the end of extraction, and the weight text continues to show the final weight, until the page is replaced by the shot review or idle screen

#### Scenario: Cup lifted off the scale during the hold

- **WHEN** the cup or portafilter is removed from the scale during the post-shot hold, driving the live scale weight to zero or below
- **THEN** the cup fill, crema and weight text are unchanged, still showing the held end-of-extraction values

#### Scenario: Cup lifted at the moment extraction ends

- **WHEN** the cup is removed from the scale during `Ending`, before the machine has left the espresso cycle, so the live reading is negative at the instant the hold begins
- **THEN** the held weight is the peak weight reached during extraction, not the negative reading, and the cup shows the extracted shot

#### Scenario: Drip after the stop raises the held fill

- **WHEN** the puck continues to drip into the cup after the machine has left the espresso cycle, while the hold is displayed
- **THEN** the held weight rises to follow it, so the cup agrees with the yield recorded for the shot and with the page's live readout beside it

#### Scenario: Shot stopped early holds its partial fill

- **WHEN** an extraction is stopped before reaching the target weight, after flow has begun, and the espresso page remains displayed after the machine leaves the espresso cycle
- **THEN** the cup holds the partial fill and the weight reached at the stop, under the same rules as a completed shot
- **NOTE** the early-stop path may navigate to the shot review immediately rather than waiting behind the post-shot stop overlay, in which case the view is destroyed at once and the hold is never visible. The hold is not conditional on how the shot ended; only its visibility is.

#### Scenario: Extraction aborted before any flow leaves the cup empty

- **WHEN** an espresso cycle is entered and then aborted during `EspressoPreheating`, without ever reaching a flow phase (`Preinfusion`, `Pouring` or `Ending`)
- **THEN** the cup renders empty, and no held weight is displayed, regardless of what the scale reports

#### Scenario: A new shot clears the previous hold

- **WHEN** a new espresso cycle begins on a cup fill view instance that is still holding the previous shot's fill — reachable by starting a second shot from the group head while the post-shot stop overlay is still displayed, which keeps the espresso page and this view alive into the second shot
- **THEN** the held fill and held weight are discarded and the cup renders empty until the new extraction reaches a flow phase

#### Scenario: Residual scale weight before any extraction still renders empty

- **WHEN** the espresso page is opened with the machine in a pre-flow phase (`Ready`, `Idle`, `Heating` or `EspressoPreheating`), no extraction has been observed on that view instance, and the scale reports a non-zero weight — for example a cup left on the scale, or a residual reading carried over from a hot-water pour
- **THEN** the cup renders empty

#### Scenario: Held frame does not animate

- **WHEN** the cup fill view is holding a completed shot's fill
- **THEN** the animation timer remains stopped, and no wave, ripple or steam animation runs for the duration of the hold. Redraws caused by late drip raising the held weight are permitted; repaints driven by the machine's idle flow stream are not.

### Requirement: Pre-Flow Cup Renders Empty

The cup fill view SHALL render no liquid at all when no extraction has been observed on that view instance, regardless of what the scale reports. The crema offset applied to the fill height SHALL NOT be able to produce a visible fill from a zero fill ratio.

#### Scenario: Resting cup on the scale draws no coffee

- **WHEN** the espresso page is displayed in a pre-flow phase with a cup resting on an untared scale, so the reported weight is well above zero, and no extraction has been observed on this view instance
- **THEN** the cup renders completely empty — not a partial fill scaled from the crema offset
