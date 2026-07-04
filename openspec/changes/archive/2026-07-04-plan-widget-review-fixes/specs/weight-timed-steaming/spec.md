# weight-timed-steaming Specification (delta)

## ADDED Requirements

### Requirement: Idle-page pitcher placement prompt

The idle page SHALL show a "Place the milk pitcher on the scale" prompt below the steam pitcher pills only while all of the following hold: weight-timed steaming is enabled (`milkAutoCaptureEnabled`), steam is the selected operation, the idle page is the active page, a supported (non-flow) scale is connected, and no load is on the scale. The prompt's second line SHALL match the feedback the user will actually get: when the capture sound option (`doseCaptureSoundEnabled`) is on it SHALL reference the beep ("wait for the beep before removing"); when the sound option is off it SHALL instead ask the user to hold the pitcher until the weight registers. The prompt SHALL never instruct the user to wait for a sound that is disabled.

#### Scenario: Prompt hidden when weight-timed steaming is off

- **WHEN** weight-timed steaming is disabled and steam is selected with a connected scale
- **THEN** no pitcher-placement prompt is shown

#### Scenario: Beep wording only when the sound is enabled

- **WHEN** weight-timed steaming is on and the capture sound option is off
- **THEN** the prompt asks the user to hold the pitcher until the weight registers, without mentioning a beep

#### Scenario: Prompt clears when the pitcher is placed

- **WHEN** the user places a pitcher (load above the detection threshold) on the scale
- **THEN** the prompt disappears
