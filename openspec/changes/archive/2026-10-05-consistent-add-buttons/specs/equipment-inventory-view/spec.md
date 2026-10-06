## MODIFIED Requirements

### Requirement: Equipment window
The system SHALL provide an `EquipmentPage.qml` that lists equipment packages with `inInventory = true` as cards, mirroring `BeanInfoPage.qml`. When no packages exist it SHALL show an empty state plus the "Add [Equipment]" create button. Like the Beans and Recipes pages, the create control is an "Add" label followed by a button naming the kind with its icon, in the same style as those pages.

#### Scenario: Empty inventory
- **WHEN** the Equipment window is opened with no packages in inventory
- **THEN** it SHALL show an empty-state message and the "Add [Equipment]" create button only

#### Scenario: Populated inventory
- **WHEN** the Equipment window is opened with packages in inventory
- **THEN** it SHALL show a card per package displaying the grinder identity (brand/model, burrs as subtitle)
- **AND** each card SHALL offer edit and remove-from-inventory actions

#### Scenario: Removing a package from inventory
- **WHEN** the user removes a package from inventory
- **THEN** the package SHALL be soft-deleted (`inInventory = 0`) and SHALL disappear from the inventory list
- **AND** bags and shots pointing at it SHALL continue to resolve to its identity
