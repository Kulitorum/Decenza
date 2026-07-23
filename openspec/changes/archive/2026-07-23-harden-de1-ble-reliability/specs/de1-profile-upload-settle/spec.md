## ADDED Requirements

### Requirement: A profile upload settles before the next state change is allowed
The system SHALL wait for a settle window after a profile upload's final frame
(and tail) write is acknowledged before allowing a subsequent state change (such
as starting espresso) to proceed, so the app does not race the DE1 firmware's
internal flash write and "download in progress" flag clear.

#### Scenario: Starting espresso right after activating a recipe
- **WHEN** a recipe is activated, its profile is uploaded, and the app
  immediately queues `startEspresso()`
- **THEN** the system holds the espresso state request until the settle window
  has elapsed since the profile upload's tail write was acknowledged
- **AND** the shot then proceeds normally through preinfusion and pour instead of
  aborting to HeaterDown right after preinfusion

#### Scenario: A state change unrelated to a recent profile upload is unaffected
- **WHEN** a state change is requested and no profile upload has completed
  recently (settle window already elapsed or not applicable)
- **THEN** the system issues the state change without additional delay

### Requirement: The settle window applies uniformly at profile-upload completion, not per caller
The system SHALL implement the settle window at the point where profile-upload
completion is signaled, so every current and future caller that changes state
after a profile upload is covered without needing its own guard.

#### Scenario: A new caller uploads a profile and changes state
- **WHEN** any code path uploads a profile and then requests a state change
  after upload completion is signaled
- **THEN** that state change is subject to the same settle window as the
  recipe-activation path, without that caller needing to implement its own delay
