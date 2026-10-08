# Spec Delta

## MODIFIED Requirements

### Requirement: Review-page milk button

The shot page SHALL offer a milk-weigh button immediately left of the Read TDS button, shown only while weight-timed steaming is on, at least one usable pitcher exists, a real (non-flow) scale is connected, and the page shows the most recently saved shot. A usable pitcher is an enabled pitcher preset with a saved empty-pitcher weight. At rest the button SHALL show the selected pitcher's name, followed by the captured milk weight once there is one.

#### Scenario: Button hidden without the prerequisites
- **WHEN** weight-timed steaming is off, or no usable pitcher exists, or no real scale is connected
- **THEN** the milk-weigh button is not shown

#### Scenario: Selected pitcher is not usable
- **WHEN** Heater off is the selected pitcher and the user taps the button
- **THEN** the button, which shows "Choose pitcher", selects the next usable pitcher and shows its name, with no capture started

#### Scenario: Captured weight shown
- **WHEN** 180 g of milk has been captured against "Small"
- **THEN** the button shows "Small · 180 g"

#### Scenario: An older shot opened from history
- **WHEN** the user opens a shot from history that is not the most recently saved one
- **THEN** the milk-weigh button is not shown, since no steam follows that shot
