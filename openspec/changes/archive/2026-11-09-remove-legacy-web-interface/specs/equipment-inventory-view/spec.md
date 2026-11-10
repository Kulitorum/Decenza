# Spec Delta

## MODIFIED Requirements

### Requirement: Idle-page Equipment button
The system SHALL provide an `EquipmentItem.qml` idle-page layout widget that navigates to the Equipment window, registered in all required places: `CMakeLists.txt` qml module list, `LayoutItemDelegate.qml` (render switch + label map), the layout editor palette (`LayoutCenterZone.qml`/`LayoutEditorZone.qml` widget list + chip label map).

#### Scenario: Default layout placement
- **WHEN** a fresh install builds the default layout
- **THEN** the default layout SHALL include an `equipment` item placed immediately after the `beans` item in the same zone

#### Scenario: Tapping the Equipment button
- **WHEN** the user taps the idle Equipment button
- **THEN** the Equipment window SHALL open

