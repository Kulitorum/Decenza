# Spec Delta

## ADDED Requirements

### Requirement: Preset Duration states its fallback role while weight timing is active

While weight-timed steaming is on, the steam rate is calibrated, and the selected pitcher preset has its heater on, the Steam page's Duration setting SHALL carry a short hint that the value is used when milk is not weighed. The hint SHALL be part of the Duration control's accessible name. Duration SHALL remain editable in every state. In any other state the hint SHALL NOT appear.

#### Scenario: Weight timing active

- **WHEN** weight-timed steaming is on, the steam rate is calibrated, and a heater-on pitcher is selected
- **THEN** the Duration row shows the hint, and the value can still be changed

#### Scenario: Toggle off

- **WHEN** weight-timed steaming is off
- **THEN** the Duration row shows no hint

#### Scenario: Rate not calibrated

- **WHEN** weight-timed steaming is on but the steam rate is uncalibrated
- **THEN** the Duration row shows no hint, because the fixed duration is what runs

#### Scenario: Hint follows the toggle without reopening the page

- **WHEN** the user turns weight-timed steaming on or off, or calibrates the rate, with the Steam page open
- **THEN** the hint appears or disappears immediately

#### Scenario: Screen reader

- **WHEN** weight timing is active and a screen-reader user focuses the Duration control
- **THEN** the spoken name includes that the value is used when milk is not weighed

#### Scenario: Toggling does not move the controls

- **WHEN** the user clicks the weight-timed steaming switch
- **THEN** the controls on the Steam page keep their positions, because each hint holds its line whether or not it is showing

#### Scenario: Pitcher row preview only while active

- **WHEN** weight-timed steaming is off or the steam rate is uncalibrated
- **THEN** the pitcher row shows no "Weight-timed: Ng → Ns" preview

#### Scenario: Scrollbar stays clear of the controls

- **WHEN** the Steam page content is taller than its viewport
- **THEN** the scrollbar is drawn beside the controls and does not cover the switch or any value box
