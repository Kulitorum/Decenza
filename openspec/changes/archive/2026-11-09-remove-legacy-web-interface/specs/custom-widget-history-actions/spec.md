# Spec Delta

## MODIFIED Requirements

### Requirement: Both widget editors offer the four actions

The four actions SHALL appear in the in-app Custom widget editor's action picker and in
the shared browser layout editor's action picker, with the same labels and the same
gesture coverage, so a widget configured on one surface reads and edits correctly on the
other.

#### Scenario: In-app picker lists the actions

- **WHEN** a user opens the action picker for a Custom widget in the in-app layout editor
- **THEN** the four context-filtered History actions are listed alongside "Go to History"

#### Scenario: Web picker lists the actions

- **WHEN** a user opens the action picker for a Custom widget in the web layout editor
- **THEN** the four context-filtered History actions are listed with the same labels

#### Scenario: Cross-surface round trip

- **WHEN** a widget is configured with one of these actions in the web editor and then opened in the in-app editor
- **THEN** the in-app editor shows that action selected, not a blank or unknown action


