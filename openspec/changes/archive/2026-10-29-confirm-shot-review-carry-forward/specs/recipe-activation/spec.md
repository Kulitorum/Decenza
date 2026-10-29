# Spec Delta

## MODIFIED Requirements

### Requirement: Bag or equipment change deactivates
Changing the active bag or bean, or the equipment package, SHALL deactivate the recipe. This SHALL be event-based, with no timers. Deactivation SHALL leave the recipe itself unchanged. The explicitly accepted shot-review correction described by `shot-review-carry-forward` is an exception: it relinks the active recipe and keeps it active without importing other bag settings.

#### Scenario: Equipment change deactivates the recipe
- **WHEN** the user changes the active bag or the equipment package while a recipe is active outside the confirmed shot-review correction
- **THEN** the recipe is deactivated and its stored fields are unchanged

#### Scenario: Accepted shot-review bean correction
- **WHEN** the user accepts a shot-review offer that includes updating the active recipe's bag link
- **THEN** that recipe remains active with the corrected bag
- **AND** its other fields and the current next-shot brew settings are preserved except for separately listed accepted corrections
