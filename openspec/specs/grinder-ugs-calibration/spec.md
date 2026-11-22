# grinder-ugs-calibration Specification

## Purpose
Lets an equipment package hold an optional fine and coarse calibration anchor on the UGS grind scale, checks and reports them alongside the calibration mined from history, and uses a valid pair to suggest a starting grind.

## Requirements

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
Phase 1 spread gate, when there are at least 10 mined pairs whatever their spread, or when a
valid anchor pair exists. With a valid anchor pair the key
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

#### Scenario: Plenty of history publishes a key without the spread gate

- **GIVEN** 58 mined pairs whose spread fails the Phase 1 gate, median 0.125, and no anchors
- **WHEN** the calibration is computed
- **THEN** a conversion key of 0.125 is published

#### Scenario: A little inconsistent history does not

- **GIVEN** 5 mined pairs whose spread fails the Phase 1 gate, and no anchors
- **WHEN** the calibration is computed
- **THEN** no conversion key is published

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

### Requirement: The calibration section SHALL link the original UGS guide

The calibration section SHALL offer a button that opens the original Universal
Grind Setting guide (`https://videoblurb.com/UGS/`) in an in-app
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

On the grind step, when the grind suggestion has a number for the picked profile on the selected bag and equipment, the wizard SHALL show that number in place of the direction-only hint, naming the shot it starts from in the same words as the Genius Grind popup (profile, date and time, and rating when it has one), with a "Use" action. "Use" SHALL also set the RPM to the starting shot's RPM when it carries one. It SHALL NOT fill the grind field by itself. Without a number the existing hint is unchanged.

#### Scenario: Calibrated estimate offered

- **GIVEN** a bag pulled at setting 12 on Londinium with a calibrated grinder whose range covers Rao Allongé
- **WHEN** the user picks Rao Allongé in the recipe wizard
- **THEN** the grind step shows a suggested starting grind for Rao Allongé marked as an estimate
- **AND** the grind field changes only when the user taps Use

#### Scenario: Outside the calibrated range

- **WHEN** the picked profile is outside the calibrated range and the bag has no shot on it
- **THEN** the wizard shows the existing direction-only hint and no number

#### Scenario: Rated starting shot named

- **GIVEN** the suggestion starts from a Cremina shot rated 85
- **WHEN** the user picks Rao Allongé in the recipe wizard
- **THEN** the grind step names that Cremina shot and its rating

### Requirement: A shot rated under 50 SHALL count as a bad shot

A shot rated 1 to 49 SHALL NOT be a starting shot, a profile's own setting, or a bag's latest good shot, even if it met its target weight. An unrated shot SHALL be unaffected. The conversion-key mining SHALL be unchanged.

#### Scenario: A low-rated shot that hit its weight is skipped

- **GIVEN** a bag whose latest Cremina shot hit its target weight and is rated 40, and an earlier unrated dialed-in Cremina shot at 21
- **WHEN** the suggestion for Cremina is computed
- **THEN** it is 21

#### Scenario: Only low-rated shots

- **GIVEN** a bag whose only shots are rated 30 and 45
- **WHEN** the suggestion is computed
- **THEN** there is no number and the reason is `noStart`

### Requirement: A rated shot SHALL count whatever its badges or weight

A shot rated 50 or above SHALL be eligible whatever its quality badges, target weight or the 15 g floor. It SHALL still be an espresso shot on the same equipment, with a parsable grinder setting and a profile on the UGS chart.

#### Scenario: A rated ristretto counts

- **GIVEN** a bag whose only rated shot is a 12 g Cremina rated 80
- **WHEN** the suggestion for Cremina is computed
- **THEN** it is that shot's setting

#### Scenario: A cleaning shot does not

- **GIVEN** a rated shot whose beverage type is cleaning
- **WHEN** the suggestion is computed
- **THEN** that shot is not considered

### Requirement: The calibration's starting shot SHALL follow ratings

The starting shot SHALL be the first of, in the current bag: the highest-rated shot rated 50+ (so a shot rated 75+ beats one rated 60); the latest good shot (dialed in, or rated 50+ whatever its badges or weight, and never rated under 50). Ties SHALL go to the most recent shot. A profile with no own setting SHALL get the starting shot moved by the key, only with a published key and inside the validated range.

#### Scenario: A highly rated shot on another profile starts the estimate

- **GIVEN** a bag whose shots are a Cremina (UGS 0) at 21.5 rated 85 and a later unrated dialed-in D-Flow at 20, with no Rao Allongé shots, and a published key of 1.25 whose range covers UGS 8
- **WHEN** the calibration gives Rao Allongé (UGS 8)
- **THEN** it is 31.5, starting from the Cremina shot

#### Scenario: A shot rated 60 comes before the latest unrated shot

- **GIVEN** a bag with a Cremina rated 60, a later unrated dialed-in D-Flow, and no other rated shot
- **WHEN** the calibration is computed
- **THEN** the Cremina shot is the starting shot

#### Scenario: Rating outranks a badge

- **GIVEN** the bag's highest-rated shot carries a channeling badge
- **WHEN** the calibration is computed
- **THEN** that shot is the starting shot

#### Scenario: The most recent wins a tie

- **GIVEN** two shots of the bag rated 90
- **WHEN** the calibration is computed
- **THEN** the more recent one is the starting shot

#### Scenario: Nothing rated 50 or above

- **GIVEN** a bag with no shot rated 50 or above and an unrated dialed-in shot
- **WHEN** the calibration is computed
- **THEN** the starting shot is the bag's latest good shot, as before this change

### Requirement: A dated bag SHALL be its own roast date

For a bag with a roast date, the current bag SHALL be the shots of that coffee with that roast date only. Neither an undated shot of the same coffee nor another roast date SHALL start a suggestion for it. An undated bag SHALL be the undated shots of that coffee within 90 days of its latest one.

#### Scenario: A dated bag starts from its own roast date

- **GIVEN** a bag roasted 2026-01-01 whose only Cremina shot is unrated at 19, an undated Cremina of the same coffee rated 90 from the same week, and a Cremina rated 95 from the 2025-10-01 roast
- **WHEN** the suggestion for Cremina on the 2026-01-01 bag is computed
- **THEN** it is 19 from the 2026-01-01 shot

### Requirement: A profile SHALL get the bag's own setting when it has one

A profile's own setting SHALL be the first of, in the current bag: its highest-rated shot rated 50+; its latest good shot; the highest-rated shot rated 50+ at the same UGS on any profile. The suggestion SHALL give it with or without a key and with no range check; the AI payload only with a key.

#### Scenario: A rated shot on the target profile gives its own setting

- **GIVEN** a bag with Rao Allongé shots at 29 and 29.5, unrated and dialed in, and one at 30 rated 80
- **WHEN** the suggestion for Rao Allongé is computed
- **THEN** it is 30

#### Scenario: Unrated shots give the latest one, not their median

- **GIVEN** a bag with unrated dialed-in Rao Allongé shots at 31, 30 and then 29.5
- **WHEN** the suggestion for Rao Allongé is computed
- **THEN** it is 29.5

#### Scenario: Own setting without calibration

- **GIVEN** a grinder with no published key and a bag with a Cremina shot at 21.5 rated 60 that missed its target weight
- **WHEN** the suggestion for Cremina is computed
- **THEN** it is 21.5, as the bag's own setting

#### Scenario: Same UGS on another profile, no calibration

- **GIVEN** a grinder with no published key and a bag whose only shot is a Cremina (UGS 0) at 21.5 rated 70
- **WHEN** the suggestion for Londinium (UGS 0) is computed
- **THEN** it is 21.5, from the Cremina shot

#### Scenario: No calibration, a profile never pulled

- **GIVEN** a grinder with no published key and no shot of the bag on Rao Allongé
- **WHEN** the suggestion for Rao Allongé is computed
- **THEN** there is no number and the reason is `notCalibrated`
- **AND** the suggestion reports the move as a UGS difference from the starting shot

### Requirement: The suggestion SHALL cover every case the Genius Grind prototype did

Wherever Mark's prototype (`fredphoesh/Decenza-Descendant` at `97daea2e3`) gave a number, the grind suggestion SHALL give one too, except where it built on a shot the user's rules exclude: a non-coffee drink, a shot rated under 50, or a coffee with no name (`noBean`). A prototype reference shot maps to rating that shot.

#### Scenario: Prototype's same-profile rated shot

- **GIVEN** an uncalibrated grinder and a bag whose best-rated shot (rated 60, badged) is a Cremina at 21.5
- **WHEN** the suggestion for Cremina is computed
- **THEN** it is 21.5, as in the prototype

#### Scenario: Prototype's same-UGS move

- **GIVEN** an uncalibrated grinder and a bag whose best-rated shot (rated 60) is a Cremina (UGS 0) at 21.5
- **WHEN** the suggestion for Damian's LRv2 (UGS 0) is computed
- **THEN** it is 21.5, as in the prototype

#### Scenario: Prototype's cross-profile rated shot

- **GIVEN** a calibrated grinder and a bag whose only rated shot is a Cremina rated 60
- **WHEN** the suggestion for a profile the bag has not been pulled on is computed
- **THEN** it gives a number, as the prototype did

### Requirement: The grind suggestion SHALL report its starting shot

A suggestion SHALL carry where it came from: a source (`rated`, `history`, `sameUgs` or `derived`, where `history` is the profile's latest good shot) and the shot it rests on (id, the shot's own profile title, setting, local time and rating). It SHALL carry the shot's RPM only when the package's grinder takes an RPM: a fixed-speed grinder's shots can hold a stale one.

#### Scenario: A derived suggestion names its shot

- **WHEN** a suggestion is derived from a Cremina shot
- **THEN** it reports that shot's id, profile, setting, time and rating

#### Scenario: Outside the range gives a reason

- **GIVEN** a published key whose range does not reach the target profile, and no shot of the bag on it
- **WHEN** the suggestion is computed
- **THEN** it carries no number and the reason `outsideRange`

#### Scenario: A fixed-speed grinder reports no RPM

- **GIVEN** a Niche Zero package whose starting shot recorded 1400 RPM
- **WHEN** the suggestion is computed
- **THEN** it reports no RPM for the shot

### Requirement: A suggestion without a number SHALL say why and how far

Without a number a suggestion SHALL carry a reason: `noEquipment`, `noBean`, `noShots`, `noStart` (no good shot of the bag yet), `profileNotOnChart`, `notCalibrated` or `outsideRange`. A history that cannot be read, or a shot with no grinder, SHALL give no number and no reason. With `notCalibrated` or `outsideRange` it SHALL also carry the move as a UGS difference from the starting shot, as the UGS calculator does when it has no key.

#### Scenario: Uncalibrated move reported in UGS

- **GIVEN** no published key and a starting shot on Cremina (UGS 0) at 21.5
- **WHEN** the suggestion for Rao Allongé (UGS 8) is computed
- **THEN** it carries no number, the reason `notCalibrated`, and a difference of 8 UGS coarser than the Cremina shot
