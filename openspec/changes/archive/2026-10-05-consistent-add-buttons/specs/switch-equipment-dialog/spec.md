## MODIFIED Requirements

### Requirement: Switch Equipment dialog
The system SHALL provide a `SwitchEquipmentDialog.qml` (mirroring `ChangeBeansDialog.qml`) that lets the user pick an existing equipment package or create a new one. It SHALL be openable from the Equipment window ("Add [Equipment]") and from Brew Settings ("Switch Equipment").

#### Scenario: Pick an existing package
- **WHEN** the user opens the dialog with packages in inventory
- **THEN** it SHALL list the packages for selection
- **AND** selecting one SHALL set it as the active bag's equipment package

#### Scenario: Create a new package
- **WHEN** the user creates a new package
- **THEN** the dialog SHALL collect grinder brand, model, and burrs with registry-backed suggestions (the same `knownGrinderBrands`/`knownGrinderModels`/`suggestedBurrs` sources the old Brew Settings grinder fields used, plus distinct values from shot history)
- **AND** `rpmCapable` SHALL be derived from the registry for the entered identity
- **AND** the new package SHALL be created and set as the active bag's equipment package
