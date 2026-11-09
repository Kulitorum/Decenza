# Spec Delta

## ADDED Requirements

### Requirement: The Early access opt-in defaults to Stable
The Early access opt-in SHALL be persisted as `firmware/EA`, and the selected channel SHALL be Stable when that key is absent or false.

#### Scenario: Absent opt-in selects Stable
- **WHEN** `firmware/EA` is absent or false
- **THEN** the selected firmware channel is Stable

#### Scenario: The opt-in persists across launches
- **WHEN** the user enables Early access and the app is relaunched
- **THEN** `firmware/EA` is still true and the Early access channel is still selected

## REMOVED Requirements

### Requirement: The Early access opt-in is a fresh preference
**Reason**: Its one-time reset of the historical nightly preference shipped in 2.0.3 and has already run on every installation at or above the upgrade floor (2.0.7).
**Migration**: None. Replaced by "The Early access opt-in defaults to Stable".
