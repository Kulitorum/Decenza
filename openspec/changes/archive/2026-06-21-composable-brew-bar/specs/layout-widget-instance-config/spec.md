## ADDED Requirements

### Requirement: Per-instance widget configuration in the editors

The layout editors SHALL allow a configurable widget instance to be opened for editing — by long-press in the in-app editor and by the existing open/edit affordance in the web editor — and SHALL persist per-instance settings using the existing item-property mechanism (`setItemProperty` / `getItemProperties` and the `/api/layout/item` endpoints). Two instances of the same widget type SHALL be able to hold different settings.

#### Scenario: Long-press opens the instance editor in-app

- **WHEN** a user long-presses a configurable widget instance in the in-app layout editor
- **THEN** an editor popup SHALL open exposing that widget's configurable properties for that instance

#### Scenario: Web editor opens the instance editor

- **WHEN** a user opens a configurable widget instance in the web layout editor
- **THEN** the editor SHALL load that instance's current properties and present controls to change them

#### Scenario: Settings persist per instance

- **WHEN** a user changes a configurable property on one instance and saves
- **THEN** that instance SHALL retain its setting across app restarts
- **AND** other instances of the same widget type SHALL be unaffected

### Requirement: Configurable data mode for the scale weight widget

The existing `scaleWeight` widget SHALL gain a per-instance `dataMode` property with the values `gross`, `netBeans`, `netMilk`, and `contextAware`. The default SHALL preserve the widget's current behaviour so existing layouts are unchanged.

#### Scenario: Default preserves current behaviour

- **WHEN** a `scaleWeight` widget has no `dataMode` set (existing layouts)
- **THEN** it SHALL behave exactly as it does today

#### Scenario: Gross mode

- **WHEN** `dataMode` is `gross`
- **THEN** the widget SHALL display the raw scale weight

#### Scenario: Net beans mode

- **WHEN** `dataMode` is `netBeans`
- **THEN** the widget SHALL display the scale weight minus the dose-cup tare (`Settings.brew.doseCupTareWeight`), clamped at zero

#### Scenario: Net milk mode

- **WHEN** `dataMode` is `netMilk`
- **THEN** the widget SHALL display the scale weight minus the selected steam pitcher's empty weight, clamped at zero

#### Scenario: Context-aware mode

- **WHEN** `dataMode` is `contextAware`
- **THEN** the widget SHALL display net milk while the machine is in a steam context and net beans otherwise

#### Scenario: Mode is selected in the editor

- **WHEN** a user opens a `scaleWeight` instance in either editor
- **THEN** the editor SHALL present the four data modes for selection
- **AND** the chosen mode SHALL persist for that instance only

#### Scenario: No scale connected

- **WHEN** no scale is connected
- **THEN** the widget SHALL display a placeholder ("—") regardless of `dataMode`
