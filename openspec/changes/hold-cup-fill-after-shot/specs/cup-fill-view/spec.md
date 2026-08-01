## ADDED Requirements

### Requirement: Cup Fill Persists After Extraction Ends

The cup fill view SHALL continue to render the liquid fill, crema and its weight text after the machine leaves the espresso cycle, for as long as the view exists, provided extraction was observed on that view instance. The rendered fill SHALL be drawn from the weight held at the moment extraction ended, not from the live scale reading, and the view's own weight text SHALL show that same held value so the number and the fill agree.

The held state SHALL be cleared when a new espresso cycle begins on the same view instance, so a subsequent shot starts from an empty cup.

The view SHALL NOT resume its animation timer for the hold; the held cup is a static final frame.

#### Scenario: Completed shot holds its cup until navigation

- **WHEN** an espresso finishes and the machine phase leaves the espresso cycle (to `Ready`, `Idle` or `Heating`) while the espresso page is still displayed during the post-shot stop overlay
- **THEN** the cup continues to show the coffee and crema at the fill level reached at the end of extraction, and the weight text continues to show the final weight, until the page is replaced by the shot review or idle screen

#### Scenario: Cup lifted off the scale during the hold

- **WHEN** the cup or portafilter is removed from the scale during the post-shot hold, driving the live scale weight to zero or below
- **THEN** the cup fill, crema and weight text are unchanged, still showing the held end-of-extraction values

#### Scenario: Shot stopped early holds its partial fill

- **WHEN** an extraction is stopped before reaching the target weight, after flow has begun, and the espresso page remains displayed after the machine leaves the espresso cycle
- **THEN** the cup holds the partial fill and the weight reached at the stop, under the same rules as a completed shot
- **NOTE** the early-stop path may navigate to the shot review immediately rather than waiting behind the post-shot stop overlay, in which case the view is destroyed at once and the hold is never visible. The hold is not conditional on how the shot ended; only its visibility is.

#### Scenario: Extraction aborted before any flow leaves the cup empty

- **WHEN** an espresso cycle is entered and then aborted during `EspressoPreheating`, without ever reaching `Preinfusion` or `Pouring`
- **THEN** the cup renders empty, and no held weight is displayed, regardless of what the scale reports

#### Scenario: A new shot clears the previous hold

- **WHEN** a new espresso cycle begins on a cup fill view instance that is still holding the previous shot's fill
- **THEN** the held fill and held weight are discarded and the cup renders empty until the new extraction reaches `Preinfusion` or `Pouring`

#### Scenario: Residual scale weight before any extraction still renders empty

- **WHEN** the espresso page is opened with the machine in a pre-flow phase (`Ready`, `Idle`, `Heating` or `EspressoPreheating`), no extraction has been observed on that view instance, and the scale reports a non-zero weight — for example a cup left on the scale, or a residual reading carried over from a hot-water pour
- **THEN** the cup renders empty

#### Scenario: Held frame is static

- **WHEN** the cup fill view is holding a completed shot's fill
- **THEN** the animation timer remains stopped, and no wave, ripple or steam animation runs for the duration of the hold
