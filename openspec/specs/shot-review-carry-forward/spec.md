# shot-review-carry-forward Specification

## Purpose
Protect the next-shot setup while offering explicitly approved corrections from the most recent shot.

## Requirements

### Requirement: Shot review saves without changing the next shot
Edits in shot review SHALL save to the completed shot without changing the next-shot setup, active bag or recipe, or their stored dial-in values. The system SHALL apply eligible corrections to the next shot only after explicit acceptance of the carry-forward offer.

#### Scenario: Correcting the most recent shot's beans
- **WHEN** the user selects different beans in the latest shot's review
- **THEN** the shot's bean snapshot is corrected
- **AND** the next-shot bag, recipe, dose, grind, equipment, temperature and yield remain unchanged

#### Scenario: Correcting grind before leaving
- **WHEN** the user corrects grind in the latest shot's review
- **THEN** the correction is saved to that shot
- **AND** the next-shot grind, active bag and recipe are unchanged until the offer is accepted

### Requirement: Offer only when the next setup still matches the shot
The system SHALL offer carry-forward only for the latest saved shot when the current next-shot setup matches its baseline. The baseline is captured at shot start while the app remains running; after restart, it is captured from the current setup when the latest shot's review opens. Differences in profile, recipe, bag, equipment, bean identity, dose, grind, RPM, barista, temperature, yield mode or anchor SHALL suppress the offer. A value restored to its baseline counts as unchanged.

#### Scenario: One independently changed field suppresses all corrections
- **GIVEN** the app still holds the latest shot's shot-start baseline and the next-shot grind differs from it
- **WHEN** the user corrects that shot's beans and exits review
- **THEN** the shot correction is saved and no carry-forward is offered
- **AND** the next-shot setup and stored bag and recipe remain unchanged

#### Scenario: A value changed and restored
- **GIVEN** the user changed the next-shot grind and then restored it to the applicable baseline value
- **WHEN** the user makes an eligible correction and exits review
- **THEN** the restored grind does not by itself suppress the offer

#### Scenario: Same grams with different anchors
- **GIVEN** the latest shot used 18 g with a 1:2 ratio and the next shot now uses an absolute 36 g target
- **WHEN** the latest shot is corrected
- **THEN** no carry-forward is offered, because the anchor mode differs

#### Scenario: Another shot becomes the latest
- **WHEN** a newer shot is saved before the offer is applied
- **THEN** corrections to the formerly latest shot are not offered or applied to the next shot

#### Scenario: Review after restart
- **GIVEN** the shot-start snapshot is unavailable after an app restart
- **WHEN** the latest shot is opened for review
- **THEN** the current next-shot setup becomes the comparison baseline
- **AND** an eligible correction may be offered with its actual proposed changes if that setup remains unchanged

#### Scenario: Next-shot setup changes during the pour
- **WHEN** a setting changes after shot start and remains different when review opens
- **THEN** no corrections are offered for the next shot in that app session

### Requirement: Confirm only the actual corrections and their effects
The offer SHALL list each eligible net correction and the resulting next-shot values and persistent recipe, bag or equipment dial changes. It SHALL exclude feedback and measurements that describe the completed shot. An edit reverted to its opening value SHALL not be offered. If no eligible net correction would change the next setup, no offer SHALL be shown.

#### Scenario: Dose under a ratio
- **GIVEN** the next shot is 18 g at 1:3, stopping at 54 g
- **WHEN** the shot dose is corrected to 19 g and the setup otherwise matches
- **THEN** the offer shows dose 18 to 19 g and stop-at 54 to 57 g with the 1:3 anchor unchanged

#### Scenario: Outcome and feedback only
- **WHEN** only rating, taste, notes, measured output, TDS or EY are changed
- **THEN** the changes are saved to the shot and no next-shot offer is shown

#### Scenario: A correction is undone
- **WHEN** a shot's grind is edited and restored to its value at review entry
- **THEN** that grind is not offered for the next shot

### Requirement: Explicit acceptance applies only the listed changes
The confirmation SHALL offer keeping the next shot unchanged or applying the displayed proposal. Accepting SHALL apply only the listed corrections and explicitly disclosed persistent effects; declining SHALL change no next-shot setting. A bag selection SHALL neither load its other settings nor write preserved settings to that bag as a consequence of selection.

#### Scenario: Corrected bag with unrelated saved dial-in values
- **GIVEN** the next shot uses 19 g, grind 10 and a 59 g absolute target
- **AND** the corrected bag stores a different dose, grind, equipment and yield
- **WHEN** the user accepts a bean-only correction
- **THEN** the next shot uses the corrected bag and retains 19 g, grind 10, its equipment and 59 g absolute target
- **AND** the corrected bag's other saved settings are not rewritten

#### Scenario: Corrected bag on an active recipe
- **WHEN** the user accepts a bean correction that explicitly lists an active recipe's bag-link update
- **THEN** the recipe remains active and is linked to the corrected bag
- **AND** the bag change does not alter the recipe's other fields or the live brew settings

#### Scenario: Accepted dial correction survives equipment selection
- **WHEN** the user accepts a grind or RPM correction with equipment selected
- **THEN** the confirmation discloses the corrected equipment package's saved dial values
- **AND** those explicitly corrected fields are saved with the bag and recipe changes
- **AND** reselecting that equipment preserves the accepted dial
- **AND** a bean-only correction does not write equipment dial values

### Requirement: Revalidate the offer and fail safely
The system SHALL revalidate the shot's latest status and the next-shot setup before applying an offer. If the setup changes while the offer is open, the offer SHALL be withdrawn without applying it. Failed or unavailable writes SHALL be reported; the shot correction SHALL remain saved and the next-shot setup SHALL not be left partially updated.

#### Scenario: Next shot changed while the offer is open
- **WHEN** the next-shot yield or any other relevant field changes before acceptance
- **THEN** the offer is withdrawn and no carry-forward is applied

#### Scenario: Recipe relink fails
- **WHEN** an accepted bean correction requires a recipe relink and that update fails
- **THEN** the failure is reported and neither the active bag nor other next-shot settings are partially changed
- **AND** the shot's own saved correction remains

### Requirement: Machine operations and disconnection take precedence
The system SHALL never apply a pending offer automatically. A new machine operation, loss of the review session or disconnection SHALL dismiss or abandon the offer while preserving the shot's saved edits and the existing next-shot setup.

#### Scenario: Machine starts while the offer is visible
- **WHEN** a machine operation starts before the user accepts
- **THEN** the operation proceeds and the offer is dismissed without applying its changes
