# Spec Delta

## MODIFIED Requirements

### Requirement: The shot page steps between shots from a list

When opened from a list of shots, the shot page SHALL let the user step to the newer and older shot in that list by swiping the graph and by Newer/Older buttons, and SHALL show the position in the list. Pending edits SHALL be saved before the step. The undo history SHALL belong to the shot it was made on. When stepping away from the latest shot, any eligible carry-forward SHALL be confirmed before the step under `shot-review-carry-forward`.

#### Scenario: Swiping to the older shot after an edit

- **GIVEN** the user opened a shot from Shot History and changed its rating
- **WHEN** the user swipes to the older shot
- **THEN** the rating change is saved to the first shot
- **AND** the page shows the older shot with nothing to undo

#### Scenario: Stepping away with an eligible correction

- **GIVEN** the latest shot has an eligible corrected dose and the next setup still matches its baseline
- **WHEN** the user steps to an older shot
- **THEN** the shot is saved and the carry-forward offer appears before the step
- **AND** either choice completes the step after its requested action

#### Scenario: Opened without a list

- **WHEN** the page is opened for one shot only, such as after pulling it
- **THEN** no Newer/Older control is shown

## ADDED Requirements

### Requirement: Leaving the latest shot offers eligible corrections
Back, system Back and auto-close SHALL save the shot and show any eligible carry-forward offer before leaving. The auto-close timeout SHALL wait for the user's choice when an offer is shown; it SHALL never apply the offer automatically. When no offer is eligible the page SHALL close normally.

#### Scenario: Auto-close with eligible corrections
- **WHEN** the review timeout expires with an eligible correction and an unchanged next setup
- **THEN** the shot is saved and the offer remains open for the user's choice
- **AND** no next-shot change occurs without acceptance

#### Scenario: Auto-close after next-shot setup changed
- **WHEN** the review timeout expires after the next-shot setup diverges from its applicable baseline
- **THEN** the shot is saved and review closes without an offer or next-shot changes
