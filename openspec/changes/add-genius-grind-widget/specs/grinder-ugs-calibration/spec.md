# Spec Delta

## ADDED Requirements

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

Without a number a suggestion SHALL carry a reason: `noEquipment`, `noBean`, `noShots`, `noStart` (no good shot of the bag yet), `profileNotOnChart`, `notCalibrated` or `outsideRange`. With `notCalibrated` or `outsideRange` it SHALL also carry the move as a UGS difference from the starting shot, as the UGS calculator does when it has no key.

#### Scenario: Uncalibrated move reported in UGS

- **GIVEN** no published key and a starting shot on Cremina (UGS 0) at 21.5
- **WHEN** the suggestion for Rao Allongé (UGS 8) is computed
- **THEN** it carries no number, the reason `notCalibrated`, and a difference of 8 UGS coarser than the Cremina shot

## MODIFIED Requirements

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
