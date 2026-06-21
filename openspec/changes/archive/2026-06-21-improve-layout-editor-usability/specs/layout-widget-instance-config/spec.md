## MODIFIED Requirements

### Requirement: Per-instance widget configuration in the editors

The layout editors SHALL allow a configurable widget instance to be opened for editing and SHALL persist per-instance settings using the existing item-property mechanism (`setItemProperty` / `getItemProperties` and the `/api/layout/item` endpoints). Two instances of the same widget type SHALL be able to hold different settings.

The instance editor SHALL be openable through an **explicit, visible affordance** — not a hidden gesture alone. In the in-app editor this affordance SHALL be visible on the widget (for example an options control on the chip), and long-press SHALL be retained as an additional shortcut. In the web editor the existing visible open/edit affordance SHALL continue to open the instance editor.

#### Scenario: Visible affordance opens the instance editor in-app

- **WHEN** a user activates the visible options affordance on a configurable widget chip in the in-app layout editor
- **THEN** an editor popup SHALL open exposing that widget's configurable properties for that instance

#### Scenario: Long-press still opens the instance editor in-app

- **WHEN** a user long-presses a configurable widget instance in the in-app layout editor
- **THEN** the same instance editor popup SHALL open

#### Scenario: Web editor opens the instance editor

- **WHEN** a user opens a configurable widget instance in the web layout editor
- **THEN** the editor SHALL load that instance's current properties and present controls to change them

#### Scenario: Settings persist per instance

- **WHEN** a user changes a configurable property on one instance and saves
- **THEN** that instance SHALL retain its setting across app restarts
- **AND** other instances of the same widget type SHALL be unaffected

## ADDED Requirements

### Requirement: Visible indicator that a widget has options

Both editors SHALL show a persistent visual indicator on every widget instance whose type is configurable, so a user can tell which widgets have options without selecting or guessing. The indicator SHALL be present whether or not the widget is selected, and absent on widget types that have no options. In the in-app editor the indicator SHALL be an SVG icon asset (not a Unicode glyph) and SHALL carry an accessible name.

#### Scenario: Configurable widget shows the indicator

- **WHEN** a zone contains a widget whose type has configurable options (for example `custom`, `scaleWeight`, `shotPlan`, `sleep`, `machineStatus`, `temperature`, `steamTemperature`, or a screensaver type)
- **THEN** that widget's chip SHALL display the has-options indicator in both editors

#### Scenario: Non-configurable widget shows no indicator

- **WHEN** a zone contains a widget whose type has no configurable options (for example `flush` or `separator`)
- **THEN** that widget's chip SHALL NOT display the has-options indicator

#### Scenario: Indicator is visible without selection

- **WHEN** a configurable widget is not selected
- **THEN** the has-options indicator SHALL still be visible on its chip

### Requirement: Single source of truth for configurable widget types

The set of widget types that have configurable options SHALL be defined in one place and consumed by every site that needs it — the in-app has-options indicator, the in-app open-options gesture/affordance, and the web editor's indicator and open affordance. Adding a new configurable widget type SHALL require updating only that single definition for the editors' "has options" behavior to stay consistent.

#### Scenario: Indicator and open behavior agree

- **WHEN** the editors render and a widget type is defined as configurable in the single source of truth
- **THEN** that type SHALL both display the has-options indicator AND respond to the open-options affordance/gesture

#### Scenario: Non-configurable type is inert everywhere

- **WHEN** a widget type is not in the single source of truth
- **THEN** it SHALL neither show the indicator nor open an instance editor in either editor
