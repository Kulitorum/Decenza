## MODIFIED Requirements

### Requirement: Measured milk widget

The layout palette SHALL provide a `milkWeight` widget that displays the measured milk weight. While a steam session is in progress and a live in-session milk measurement is available, the widget SHALL display that live value; otherwise it SHALL display the most recent committed session weight (`Settings.brew.lastSteamMilkG`). This lets the widget give live feedback during steaming instead of remaining at the last completed session's value.

#### Scenario: Live milk while steaming

- **WHEN** a steam session is in progress and a live in-session milk weight greater than zero is available
- **THEN** the widget SHALL display that live in-session weight in grams

#### Scenario: Milk has been measured (idle)

- **WHEN** no live in-session milk weight is available and a committed milk weight greater than zero exists
- **THEN** the widget SHALL display the committed milk weight in grams

#### Scenario: No milk measured or weight-timed steaming absent

- **WHEN** no live and no committed milk weight is available (including when the weight-timed-steaming feature is not present)
- **THEN** the widget SHALL display a placeholder ("—") and SHALL NOT error
