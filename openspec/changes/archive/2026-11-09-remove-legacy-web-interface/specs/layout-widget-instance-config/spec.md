# Spec Delta

## MODIFIED Requirements

### Requirement: Per-instance widget configuration in the editors
The layout editors SHALL allow a configurable widget instance to be opened for editing and SHALL persist per-instance settings using the existing item-property mechanism (through the shared layout service). Two instances of the same widget type SHALL be able to hold different settings.

#### Scenario: Visible affordance opens the instance editor in-app

- **WHEN** a user activates the visible options affordance on a configurable widget chip in the in-app layout editor
- **THEN** an editor popup SHALL open exposing that widget's configurable properties for that instance

#### Scenario: Long-press still opens the instance editor in-app

- **WHEN** a user long-presses a configurable widget instance in the in-app layout editor
- **THEN** the same instance editor popup SHALL open

#### Scenario: Web gear button opens the instance editor

- **WHEN** a user clicks the gear on a configurable widget chip in the web layout editor
- **THEN** the instance editor SHALL open loaded with that instance's current properties
- **AND** this SHALL hold whether or not the chip was already selected

#### Scenario: Repeated chip clicks never dead-end the options flow

- **WHEN** a user opens a widget's options, closes the editor, and activates the options affordance again
- **THEN** the instance editor SHALL reopen — no alternating click SHALL be a no-op

#### Scenario: Editor closes with its widget

- **WHEN** the widget whose instance editor is open is deselected or removed
- **THEN** the instance editor SHALL close

#### Scenario: Settings persist per instance

- **WHEN** a user changes a configurable property on one instance and saves
- **THEN** that instance SHALL retain its setting across app restarts
- **AND** other instances of the same widget type SHALL be unaffected

