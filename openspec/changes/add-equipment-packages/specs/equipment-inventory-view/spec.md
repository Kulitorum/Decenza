## ADDED Requirements

### Requirement: Equipment window
The system SHALL provide an `EquipmentPage.qml` that lists equipment packages with `inInventory = true` as cards, mirroring `BeanInfoPage.qml`. When no packages exist it SHALL show an empty state plus an "Add Equipment" button.

#### Scenario: Empty inventory
- **WHEN** the Equipment window is opened with no packages in inventory
- **THEN** it SHALL show an empty-state message and an "Add Equipment" button only

#### Scenario: Populated inventory
- **WHEN** the Equipment window is opened with packages in inventory
- **THEN** it SHALL show a card per package displaying the grinder identity (brand/model, burrs as subtitle)
- **AND** each card SHALL offer edit and remove-from-inventory actions

#### Scenario: Removing a package from inventory
- **WHEN** the user removes a package from inventory
- **THEN** the package SHALL be soft-deleted (`inInventory = 0`) and SHALL disappear from the inventory list
- **AND** bags and shots pointing at it SHALL continue to resolve to its identity

### Requirement: Idle-page Equipment button
The system SHALL provide an `EquipmentItem.qml` idle-page layout widget that navigates to the Equipment window, registered in all required places: `CMakeLists.txt` qml module list, `LayoutItemDelegate.qml` (render switch + label map), the layout editor palette (`LayoutCenterZone.qml`/`LayoutEditorZone.qml` widget list + chip label map), and `shotserver_layout.cpp` (web editor widget list).

#### Scenario: Default layout placement
- **WHEN** a fresh install builds the default layout
- **THEN** the default layout SHALL include an `equipment` item placed immediately after the `beans` item in the same zone

#### Scenario: Tapping the Equipment button
- **WHEN** the user taps the idle Equipment button
- **THEN** the Equipment window SHALL open

### Requirement: Idempotent layout-injection migration for existing users
`getLayoutObject()` in `src/core/settings_network.cpp` SHALL run a run-once migration that ensures an `equipment` item exists in the saved layout: if no item across any zone has `type == "equipment"`, it SHALL insert `{"type":"equipment","id":"equipment1"}` immediately after the `beans` item (or, if no `beans` item exists, append it to `bottomRight`), then persist the migrated layout once.

#### Scenario: Existing customized layout gains the button
- **WHEN** an upgraded user has a saved layout with no equipment item
- **THEN** the migration SHALL inject an equipment item next to their beans item and persist it once
- **AND** the equipment button SHALL be initially visible and movable thereafter

#### Scenario: Migration is idempotent
- **WHEN** the layout already contains an equipment item
- **THEN** the migration SHALL make no change and SHALL NOT add a duplicate

#### Scenario: Beans item absent
- **WHEN** the saved layout has no beans item
- **THEN** the equipment item SHALL be appended to the `bottomRight` zone
