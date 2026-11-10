# Spec Delta

## MODIFIED Requirements

### Requirement: Shared preparation workflow selection
Native and Wasm SHALL share one host-owned preparation selection for espresso, steam, hot water, and flush. Selection SHALL update preparation controls without changing any client's navigation, dialogs, or drafts, and SHALL NOT start an operation.

#### Scenario: Select a preparation workflow from another interface
- **WHEN** a user selects Steam in a browser while native Decenza is showing an editor, or selects Espresso on native Decenza while a browser is reviewing history
- **THEN** all interfaces show the shared selected preparation workflow and its matching preparation controls
- **AND** each interface remains on its current route with its open dialog and unsaved draft intact
- **AND** no machine operation starts from the selection alone

