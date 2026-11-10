# Spec Delta

## MODIFIED Requirements

### Requirement: Both editors expose gesture overrides

The in-app layout editor and the shared browser layout editor SHALL both offer gesture
editing for these widget types, with the same actions, the same reserved-slot presentation,
and the same stored result. A widget configured in one editor SHALL read back correctly in
the other.

The types SHALL be marked as having per-instance options, so the existing has-options
indicator appears on them.

#### Scenario: Indicator appears on a built-in widget

- **WHEN** the layout editor shows a Beans widget
- **THEN** it carries the same has-options affordance as other configurable widgets

#### Scenario: Cross-surface round trip

- **WHEN** a gesture override is set in the web editor and the widget is then opened in the in-app editor
- **THEN** the in-app editor shows that action selected, and the same slot reserved

#### Scenario: Reserved slot presented consistently

- **WHEN** a one-slot widget with an override is opened in either editor
- **THEN** both show the reserved gesture as non-editable and name the page it opens


