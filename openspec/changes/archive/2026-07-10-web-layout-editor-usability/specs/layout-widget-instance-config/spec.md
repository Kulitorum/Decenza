# layout-widget-instance-config Delta

## MODIFIED Requirements

### Requirement: Per-instance widget configuration in the editors

The layout editors SHALL allow a configurable widget instance to be opened for editing and SHALL persist per-instance settings using the existing item-property mechanism (`setItemProperty` / `getItemProperties` and the `/api/layout/item` endpoints). Two instances of the same widget type SHALL be able to hold different settings.

The instance editor SHALL be openable through an **explicit, visible affordance** — not a hidden gesture alone. In the in-app editor this affordance SHALL be visible on the widget (for example an options control on the chip), and long-press SHALL be retained as an additional shortcut. In the web editor the has-options indicator (gear) SHALL itself be an activatable control that opens the instance editor directly. Activating the options affordance SHALL open the editor regardless of the chip's current selection state — selection toggling SHALL NOT swallow or invert the open action. An open instance editor SHALL close when its widget is deselected or removed, so the editor is never open for a widget that is no longer selected.

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

## ADDED Requirements

### Requirement: Web readout options match the in-app editor's presentation

The web layout editor SHALL present readout widget options in a dedicated editor with the same structure and wording as the in-app readout options editor: labeled sections per option key (for example "Scale data mode", "Display", "Color"), the same descriptive choice labels (for example "Net beans (minus dose tare)", "Context-aware (milk while steaming, else beans)"), and the same explanatory hints (for example the show-ratio hint). The sections shown SHALL continue to derive from the shared readout capability schema. Unlabeled inline controls on the chip SHALL NOT be the only way to edit readout options.

#### Scenario: Web readout editor shows labeled sections

- **WHEN** a user opens the options editor for a readout widget (e.g. Scale Weight) on the web
- **THEN** each option the type supports SHALL appear under a labeled section matching the in-app editor
- **AND** choices SHALL use the same descriptive labels as the in-app editor

#### Scenario: Sections still derive from the capability schema

- **WHEN** an option key is added to a type's entry in the readout capability schema
- **THEN** the web readout options editor SHALL show the corresponding section without a separate web-side type list being edited
