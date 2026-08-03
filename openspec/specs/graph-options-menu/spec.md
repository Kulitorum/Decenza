# graph-options-menu Specification

## Purpose
TBD - created by archiving change add-graph-flow-scale-menu. Update Purpose after archive.
## Requirements
### Requirement: Graph Options Menu

The shot detail and post-shot review graphs SHALL present their graph display options in a
menu, opened from the control that presently toggles advanced curves. The bare toggle button
SHALL be replaced by this menu; advanced curves SHALL become an option inside it.

#### Scenario: Button opens the menu
- **WHEN** the user activates the graph options control on the shot detail or post-shot review
  page
- **THEN** a menu SHALL open
- **AND** activating the control SHALL NOT itself toggle advanced curves

#### Scenario: Menu contents
- **WHEN** the graph options menu is open
- **THEN** it SHALL offer an advanced curves toggle
- **AND** it SHALL offer a flow scale selector with the choices 1x, 2x and 3x
- **AND** the flow scale selector SHALL indicate which value is currently active

#### Scenario: Advanced curves keeps its existing behaviour
- **WHEN** the user changes the advanced curves option from inside the menu
- **THEN** the effect SHALL be identical to the previous button, including which series and
  panels it reveals and the setting it writes

#### Scenario: Selection applies without dismissing
- **WHEN** the user selects a flow scale from the menu
- **THEN** the graph behind the menu SHALL re-render at that scale

#### Scenario: Menu follows the established selector pattern
- **WHEN** the graph options menu is presented
- **THEN** it SHALL use the same option-card presentation, theming and dismissal behaviour as
  the live espresso screen's extraction view selector

### Requirement: Flow Scale On The Live Espresso Screen

The live espresso screen SHALL offer the flow scale from its existing extraction view
selector rather than gaining a second menu.

#### Scenario: Selector gains the flow scale
- **WHEN** the extraction view selector is open and the chart view is the current mode
- **THEN** it SHALL offer the same flow scale choices as the graph options menu

#### Scenario: Hidden when no chart is shown
- **WHEN** the extraction view selector is open and a non-chart view is the current mode
- **THEN** the flow scale selector SHALL NOT be offered

#### Scenario: One setting behind both surfaces
- **WHEN** the flow scale is changed from either the extraction view selector or the graph
  options menu
- **THEN** both surfaces SHALL subsequently show the same active value

### Requirement: Graph Options Menu Accessibility

The menu and its options SHALL be operable by screen reader and keyboard.

#### Scenario: Control announces that it opens a menu
- **WHEN** a screen reader focuses the graph options control
- **THEN** its accessible name SHALL identify it as opening graph options rather than as a
  toggle

#### Scenario: Options expose role and state
- **WHEN** a screen reader focuses an option inside the menu
- **THEN** the option SHALL expose an appropriate role, its accessible name, and its selected
  or checked state

#### Scenario: Keyboard reachable
- **WHEN** the user navigates the page by keyboard
- **THEN** the graph options control SHALL be reachable in the page's focus order
- **AND** every option inside the open menu SHALL be reachable and activatable

