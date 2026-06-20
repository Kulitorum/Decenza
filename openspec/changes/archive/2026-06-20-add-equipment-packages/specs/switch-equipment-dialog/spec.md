## ADDED Requirements

### Requirement: Switch Equipment dialog
The system SHALL provide a `SwitchEquipmentDialog.qml` (mirroring `ChangeBeansDialog.qml`) that lets the user pick an existing equipment package or create a new one. It SHALL be openable from the Equipment window ("Add Equipment") and from Brew Settings ("Switch Equipment").

#### Scenario: Pick an existing package
- **WHEN** the user opens the dialog with packages in inventory
- **THEN** it SHALL list the packages for selection
- **AND** selecting one SHALL set it as the active bag's equipment package

#### Scenario: Create a new package
- **WHEN** the user creates a new package
- **THEN** the dialog SHALL collect grinder brand, model, and burrs with registry-backed suggestions (the same `knownGrinderBrands`/`knownGrinderModels`/`suggestedBurrs` sources the old Brew Settings grinder fields used, plus distinct values from shot history)
- **AND** `rpmCapable` SHALL be derived from the registry for the entered identity
- **AND** the new package SHALL be created and set as the active bag's equipment package

### Requirement: Switching equipment applies the package's last dial
When a package is selected as the active bag's equipment, the dialog flow SHALL apply that package's `lastGrindSetting`/`lastRpm` to the active bag's grind setting and rpm (per the equipment-package-model dual-memory rule), so Brew Settings reflects a non-blank, editable dial immediately.

#### Scenario: Selecting a package pre-fills the dial
- **WHEN** the user selects a package whose `lastGrindSetting` is "3.0" and `lastRpm` is 1200
- **THEN** the active bag's grind setting SHALL become "3.0" and rpm 1200
- **AND** both SHALL remain editable in Brew Settings

### Requirement: Editing a package from the dialog uses reference semantics
The dialog SHALL allow editing an existing package's grinder identity. Edits SHALL apply to the package row (reference semantics), so all bags and shots pointing at it resolve to the edited values; `rpmCapable` SHALL re-derive on a brand/model change.

#### Scenario: Editing identity flows to history
- **WHEN** the user edits a package's grinder model via the dialog
- **THEN** the package row SHALL be updated and all referencing bags and shots SHALL resolve to the new model
