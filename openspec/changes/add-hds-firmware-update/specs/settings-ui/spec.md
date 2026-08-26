## ADDED Requirements

### Requirement: Conditional HDS update action in Connections

Settings → Connections SHALL remain visually unchanged unless a newer eligible HDS firmware release is available for the currently selected connected Bluetooth or USB HDS. In that case, the selected-scale actions SHALL show an **Update** button immediately beside **Forget**. Activating it SHALL open an accessible confirmation dialog that displays the installed and available versions, the GitHub release notes for the available version, and an explicit action to start the update.

#### Scenario: No HDS update is available

- **WHEN** no selected connected HDS has a newer eligible release
- **THEN** Settings → Connections SHALL show no HDS update control, placeholder, banner, or error state

#### Scenario: HDS update is available

- **WHEN** the selected connected Bluetooth or USB HDS has a newer eligible release
- **THEN** an **Update** button SHALL appear immediately beside the selected scale's **Forget** button

#### Scenario: User reviews and confirms the update

- **WHEN** the user activates the HDS **Update** button
- **THEN** an accessible modal dialog SHALL present the installed version, available version, and GitHub release notes
- **AND** the dialog SHALL offer Cancel and Start update actions

#### Scenario: Selected scale changes while dialog is open

- **WHEN** the selected scale changes or the HDS disconnects before Start update is confirmed
- **THEN** the confirmation dialog SHALL close without sending an update command
