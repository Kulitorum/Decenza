# Spec Delta

## ADDED Requirements

### Requirement: Genius Grind widget

The layout palette SHALL provide a `geniusGrind` widget, placeable in bars and the centre zone, showing the grind suggestion for the loaded profile and active bag on the active equipment, or "—" without one. It SHALL refresh when the profile, bag, equipment, grind calibration or shot history (a shot saved, edited, rated or deleted) changes. Tapping it SHALL open the Genius Grind popup.

#### Scenario: Suggestion shown for the loaded profile

- **GIVEN** a calibrated grinder, an active bag, and Rao Allongé loaded
- **WHEN** the widget is on the home screen
- **THEN** it shows the suggested grind for Rao Allongé on that bag

#### Scenario: No suggestion

- **GIVEN** the grinder calibration publishes no number for the loaded profile
- **WHEN** the widget is shown
- **THEN** it shows "—"

#### Scenario: Rating a shot updates the widget

- **GIVEN** the widget shows a suggestion derived from the bag's latest shot
- **WHEN** the user rates a shot of that bag 85 on the post-shot review page
- **THEN** the widget shows the suggestion starting from the rated shot without any other change

### Requirement: The Genius Grind popup SHALL name its starting shot

The popup SHALL show the suggested grind and one line naming the shot it starts from (profile, setting, local date and time, and rating when the shot has one). Tapping that line SHALL open the shot. When the number is your own shots of this bag on that profile, the line SHALL say so instead.

#### Scenario: Starting shot named and opened

- **GIVEN** the suggestion starts from a Cremina shot at 21.5 rated 85 on Nov 19 at 4:08 AM
- **WHEN** the popup opens
- **THEN** it names that shot with its setting, date and time, and rating
- **AND** tapping the line opens that shot

### Requirement: The Genius Grind popup SHALL preview other beans and profiles

The popup SHALL offer Bean and Profile choices that open inside the popup. Beans SHALL be the bag inventory, starting at the active bag or, with none, the most recently used one. Profiles SHALL be those on the UGS chart plus the loaded one. Choosing either SHALL recalculate the suggestion without changing the active bag, profile or grind.

#### Scenario: Previewing another profile changes nothing

- **WHEN** the user picks Extractamundo in the popup's profile list
- **THEN** the popup shows the suggestion for Extractamundo
- **AND** the loaded profile, active bag and grind are unchanged

### Requirement: The Genius Grind popup SHALL offer the other reading when they disagree

When a profile's top-rated shot (rated 50+) and its latest good shot give different settings, at least one grind step apart (the grinder's history-derived step), the popup SHALL preselect the suggestion and offer the other as an alternative. Each SHALL carry a note with the facts behind it: the rating, how long ago the rated shot was, and the latest shot's setting. Otherwise only the one number SHALL show.

#### Scenario: An old rated shot and the latest shot disagree

- **GIVEN** the bag's best-rated Cremina (85) was 16 days ago at 20.5, and its latest good Cremina shot is at 21.5
- **WHEN** the popup suggests Rao Allongé with a key of 1.25
- **THEN** it preselects 30.5 (from the rated shot) and offers 31.5 (from the latest shot), each noting the rating, the 16 days and the latest setting

#### Scenario: They agree

- **GIVEN** the rated shot and the latest good shot give settings less than one grind step apart
- **WHEN** the popup opens
- **THEN** it shows a single number with no alternative

#### Scenario: Apply uses the chosen one

- **WHEN** the user picks the alternative and taps Apply
- **THEN** the grind is set to the alternative's setting, and the RPM to its shot's RPM when recorded

### Requirement: Genius Grind Apply SHALL set the bag, profile and grind

Apply SHALL activate the chosen bag, then load the chosen profile, then set the grind to the suggestion, and the RPM to the starting shot's RPM when it recorded one, each step waiting for the previous one's result. Apply SHALL be unavailable without a number. If a step fails, the popup SHALL stop, report the failure and leave the later steps undone. Cancel SHALL close it with nothing changed.

#### Scenario: Apply switches to the previewed choice

- **GIVEN** the popup previews bag B on Extractamundo with a suggestion of 24
- **WHEN** the user taps Apply
- **THEN** bag B becomes active, Extractamundo is loaded and the grind is set to 24

#### Scenario: Bag activation fails

- **WHEN** activating the chosen bag fails during Apply
- **THEN** the popup reports the failure and the profile and grind are unchanged

### Requirement: The Genius Grind popup SHALL say why there is no number

Without a suggestion the popup SHALL say why: no active equipment, no bean chosen, the current coffee needs a name (its bag has neither a roaster nor a coffee name), no usable shot of the bean, the profile is not on the UGS chart, the grinder is not calibrated yet, or the profile is outside the range the shots cover. For an uncalibrated grinder or a profile outside the range, it SHALL show the move in UGS from the starting shot.

#### Scenario: Unnamed coffee

- **GIVEN** the chosen bag has neither a roaster nor a coffee name
- **WHEN** the popup opens
- **THEN** it shows no number and says the current coffee needs to be named first

#### Scenario: Uncalibrated grinder, profile not pulled on this bag

- **GIVEN** a grinder with no published conversion key and no shot of the bag on Rao Allongé or at its UGS
- **WHEN** the popup previews Rao Allongé
- **THEN** it shows no number, says the grinder is not calibrated yet, and says Rao Allongé is 8 UGS coarser than the starting Cremina shot at 21.5

#### Scenario: Uncalibrated grinder, profile already pulled

- **GIVEN** a grinder with no published conversion key and a Cremina shot of the bag at 21.5
- **WHEN** the popup previews Cremina
- **THEN** it shows 21.5 as the bag's own setting
