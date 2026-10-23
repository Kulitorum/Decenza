## ADDED Requirements

### Requirement: An equipment package SHALL own optional fine and coarse calibration anchors

An equipment package SHALL be able to hold at most one *fine* and one *coarse*
calibration anchor, each a reference to one of the package's own shots, stored on
the package (`equipment_packages.calib_fine_shot_id`, `calib_coarse_shot_id`).
Anchors SHALL be optional: a package with none SHALL calibrate exactly as Phase 1
does. No calibration data SHALL be stored outside the package.

#### Scenario: Anchors are stored on the package

- **WHEN** the user marks a shot as the fine anchor
- **THEN** the shot's package records that shot as its fine anchor
- **AND** the package's coarse anchor, if any, is unchanged

#### Scenario: A deleted anchor shot clears the anchor

- **WHEN** a shot that is a package's anchor is deleted
- **THEN** that anchor side of the package becomes unset

#### Scenario: Anchors go away with the package

- **WHEN** a package is hard-deleted
- **THEN** its anchors are deleted with it

### Requirement: Merging packages SHALL keep the target's anchors and fill its empty sides

When two packages are merged, the target SHALL keep its own anchors and SHALL take
the source's anchor for each side it has unset.

#### Scenario: Merge keeps the target's anchors and fills gaps

- **GIVEN** a source package with fine and coarse anchors and a target with only a fine anchor
- **WHEN** the source is merged into the target
- **THEN** the target keeps its own fine anchor and takes the source's coarse anchor

### Requirement: A copy-on-write fork SHALL NOT carry anchors

A fork SHALL start with no anchors, matching the Phase 1 history scope: the fork is
different gear and has no shots yet. The superseded package SHALL keep its anchors.

#### Scenario: A fork starts without anchors

- **GIVEN** a package with anchors and at least one shot
- **WHEN** an identity edit (e.g. a burr change) forks a new package
- **THEN** the new package has no anchors and the old package keeps its anchors

### Requirement: Device transfer SHALL carry anchors with remapped ids

A device-transfer import SHALL carry anchors with their package, remapping both the
package id and the shot ids. When an imported package maps onto an existing
destination package, the destination SHALL keep its own anchors and take the
source's only for unset sides.

#### Scenario: Device transfer remaps anchors

- **WHEN** a database with an anchored package is imported and its shots receive new ids
- **THEN** the imported package's anchors reference the imported copies of its anchor shots

### Requirement: Marking an anchor SHALL refuse an ineligible shot with the reason

Marking SHALL refuse, without changing any stored anchor, a shot that is not
espresso, has no equipment package, weighs under 15 g, carries any quality badge,
has no numeric grinder setting, is on a profile with no UGS placement, or has no
bean identity. The refusal SHALL name its reason. A marked shot SHALL count as
dialed-in: the rating/yield/refractometer test SHALL NOT apply to it.

#### Scenario: Ineligible shot is refused

- **WHEN** the user marks a shot that was flagged for channeling
- **THEN** no anchor changes and the user is told the shot has a quality badge

### Requirement: Marking an anchor SHALL refuse an invalid pair with the reason

When the package's other anchor is set, marking SHALL refuse, without changing any
stored anchor, when the two shots are on different coffee batches (the Phase 1 batch
identity), at different RPM, or less than 2 UGS apart. The refusal SHALL name its
reason.

#### Scenario: Pair on different coffees is refused

- **GIVEN** a coarse anchor pulled on bag A
- **WHEN** the user marks a shot on bag B as the fine anchor
- **THEN** no anchor changes and the user is told the anchors must be on the same bag

#### Scenario: Pair too close is refused

- **GIVEN** a fine anchor on a profile at UGS 1.0
- **WHEN** the user marks a shot on a profile at UGS 2.5 as the coarse anchor
- **THEN** no anchor changes and the user is told the anchors must be at least 2 UGS apart

#### Scenario: Replacing one side keeps a valid pair intact on refusal

- **GIVEN** a valid fine/coarse pair
- **WHEN** the user marks a new fine anchor that fails the pair rules
- **THEN** the existing pair is unchanged

### Requirement: Anchors SHALL be re-checked on every read

Each calibration read SHALL re-validate the stored anchors against current data: the
anchor shot SHALL still exist, still belong to the package, and still pass the
per-shot and pair rules. An anchor that no longer does SHALL NOT contribute, and the
calibration status SHALL say which anchor and why.

#### Scenario: An edited anchor stops counting

- **GIVEN** a valid anchor pair
- **WHEN** the user later edits the coarse anchor shot onto a different bag
- **THEN** the pair no longer contributes to the conversion key
- **AND** the status reports that the anchors are on different bags

### Requirement: A valid anchor pair SHALL count as one within-batch pair

A valid anchor pair SHALL be one within-batch pair with slope
`(coarseSetting − fineSetting) / (coarseUGS − fineUGS)`; anchor shots SHALL NOT also
contribute to mined pairs. The key SHALL publish when at least 3 mined pairs pass the
Phase 1 spread gate or a valid anchor pair exists. With a valid anchor pair the key
SHALL be the median of the mined slopes plus the anchor slope, and the pair's UGS
span SHALL join the validated range as a mined pair's does.

#### Scenario: Anchors alone publish a key

- **GIVEN** a package with no mined pairs and a valid anchor pair spanning UGS 0–8
- **WHEN** the calibration is computed with a current-coffee anchor shot present
- **THEN** a conversion key is published equal to the anchor slope
- **AND** the validated range is UGS 0–8

#### Scenario: Anchors and history combine

- **GIVEN** 4 mined pairs that pass the spread gate and a valid anchor pair
- **WHEN** the calibration is computed
- **THEN** the key is the median of the 4 mined slopes and the anchor slope

### Requirement: Disagreement between anchors and history SHALL be reported, not enforced

With at least one mined pair, a valid anchor pair SHALL be reported as agreeing with
history only when its slope has the mined median's sign and lies within
`maxSpreadRatio · |minedMedian|` of it. Disagreement SHALL be reported in the
calibration status and the log, and SHALL NOT by itself withhold the key.

#### Scenario: Disagreement is reported, not hidden

- **GIVEN** mined pairs with median slope 2.0 and an anchor slope of −1.0
- **WHEN** the calibration is computed
- **THEN** the status reports that the anchors disagree with history

### Requirement: The equipment card SHALL show the package's grind calibration

Each Equipment page card SHALL show a Grind calibration section: a status line naming
the key's source (history, anchors, both) or why there is none yet, the profile range
grind suggestions cover, and one line per anchor side. A disagreement or an anchor
that no longer counts SHALL add one line saying so. The section SHALL load off the
main thread and refresh when the package's anchors change.

#### Scenario: Not enough data

- **GIVEN** a package with too little history and no anchors
- **WHEN** the Equipment page is shown
- **THEN** the card reports there is not enough data yet

### Requirement: The calibration section SHALL present anchors as optional

The section SHALL state that anchors are optional. With no anchors it SHALL say
marking a fine and a coarse shot on the same bag is an optional way to calibrate;
when history alone yields a key it SHALL say anchors are not needed.

#### Scenario: History is enough

- **GIVEN** a package whose history yields a key and no anchors
- **WHEN** the Equipment page is shown
- **THEN** the card reports the calibration comes from history, the profile range it covers, and that anchors are optional and not needed

### Requirement: An anchor line SHALL open its shot and offer a clear action

Tapping an anchor line SHALL open that shot; each anchor line SHALL offer a clear
action that unsets that side.

#### Scenario: Clearing an anchor

- **WHEN** the user clears the fine anchor on a card
- **THEN** the package's fine anchor becomes unset and the card refreshes

### Requirement: The post-shot review page SHALL offer the anchor marks

The post-shot review page SHALL show, for any espresso shot with an equipment
package — recipe shots included — the package's one-line calibration status and
"Mark as fine anchor" / "Mark as coarse anchor" actions. Pending edits on the page
SHALL be saved before the mark is applied, and the mark SHALL be evaluated against
the saved shot. A refusal SHALL be shown with its reason; a success SHALL show the
updated status.

#### Scenario: Recipe shot can be marked

- **GIVEN** a shot pulled from a recipe, with an equipment package
- **WHEN** the review page is shown
- **THEN** the mark actions are available

#### Scenario: Unsaved grind edit is saved first

- **GIVEN** the user changed the grinder setting on the review page and has not saved
- **WHEN** the user marks the shot as the fine anchor
- **THEN** the edit is saved, and the anchor eligibility uses the saved setting

### Requirement: ShotServer SHALL mirror the calibration section and mark actions

The ShotServer `/equipment` page SHALL show the same Grind calibration section on each
package card, with clear actions, and the web shot page SHALL offer the same mark
actions with the same refusal reasons. Both SHALL use the same calibration
computation and the same mark validation as the app.

#### Scenario: Mark from the web shot page

- **WHEN** the user marks a shot as the coarse anchor from the web shot page
- **THEN** the package's coarse anchor is set exactly as marking in the app would

### Requirement: The calibration section SHALL link the original UGS guide

The calibration section SHALL offer a button that opens the original Universal
Grind Setting guide (`https://videoblurb.com/UGS/ugsinfo.html`) in an in-app
dialog, loaded live from that URL with Qt WebView so it looks exactly like the
original. The dialog SHALL show the page's current URL above the page and SHALL
offer to open it in the system browser. Nothing SHALL overlap the web view.

#### Scenario: Opening the UGS guide

- **WHEN** the user taps the UGS guide button on the calibration section
- **THEN** a dialog shows the original guide page live from its own site
- **AND** the page's URL is visible above it

### Requirement: Linux SHALL open the UGS guide in the system browser

On Linux, where Qt WebView would require Qt WebEngine, the UGS guide button SHALL
open the same URL in the system browser instead of a dialog.

#### Scenario: Opening the UGS guide on Linux

- **WHEN** the user taps the UGS guide button on Linux
- **THEN** the guide opens in the system browser

### Requirement: The recipe wizard SHALL suggest a calibrated starting grind

On the grind step, when the grinder calibration publishes a number for the picked
profile — anchored on the selected bag's latest shot on the selected equipment —
the wizard SHALL show that number in place of the direction-only hint, labelled as
from the user's own shots or as an estimate, with a "Use" action. It SHALL NOT fill
the grind field by itself. Without a published number the existing hint is
unchanged. The web recipe editor is out of scope (it is being replaced).

#### Scenario: Calibrated estimate offered

- **GIVEN** a bag pulled at setting 12 on Londinium with a calibrated grinder whose range covers Rao Allongé
- **WHEN** the user picks Rao Allongé in the recipe wizard
- **THEN** the grind step shows a suggested starting grind for Rao Allongé marked as an estimate
- **AND** the grind field changes only when the user taps Use

#### Scenario: Outside the calibrated range

- **WHEN** the picked profile is outside the calibrated range
- **THEN** the wizard shows the existing direction-only hint and no number

