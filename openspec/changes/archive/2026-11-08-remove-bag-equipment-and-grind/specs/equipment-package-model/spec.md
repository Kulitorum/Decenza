# Spec Delta

## MODIFIED Requirements

### Requirement: Grinder-scoped dial memory and dual write-through
Switching the active equipment package SHALL apply that package's `lastGrindSetting`/`lastRpm` to the live grind setting and RPM (and thereby Brew Settings), never leaving them blank but leaving them editable. Editing the grind setting or rpm SHALL write through to the active package's `lastGrindSetting`/`lastRpm`.

#### Scenario: Switch equipment pre-fills the dial
- **WHEN** the user switches to a different equipment package
- **THEN** the live grind setting and RPM SHALL be set to that package's `lastGrindSetting`/`lastRpm`
- **AND** the values SHALL remain editable

#### Scenario: Editing the dial updates both memories
- **WHEN** the user edits the grind setting or rpm in Brew Settings
- **THEN** the new value SHALL be written to the active package's last-dial fields and not to the bag

### Requirement: SettingsDye exposes resolved grinder identity and equipment selection
`SettingsDye` SHALL expose `dyeGrinderBrand`, `dyeGrinderModel`, and `dyeGrinderBurrs` as **read-only** values resolved through the active equipment package. It SHALL expose `dyeGrinderSetting` and a new `dyeGrinderRpm` as writable dial-in values (writing through to the active package), and an `activeEquipmentId` for selecting/switching the package.

#### Scenario: Read resolves via the package
- **WHEN** `dyeGrinderModel` is read
- **THEN** it SHALL return the model of the active package grinder item, or empty when no package is active

### Requirement: Equipment survives backup restore and device-to-device transfer
The DB import path (`ShotHistoryStorage::importDatabaseStatic`) SHALL migrate `equipment_packages` and `equipment_items` rows and remap `shots.equipment_id` to the new package ids (alongside the existing bag-id remap). `SettingsSerializer` SHALL exclude `dye/activeEquipmentId` from export.

#### Scenario: Restoring a backup with equipment
- **WHEN** a backup containing equipment tables is restored
- **THEN** all packages and items SHALL be imported with new ids
- **AND** imported shots' `equipment_id` SHALL point at the corresponding imported packages

## REMOVED Requirements

### Requirement: coffee_bags and shots reference a package via equipment_id
**Reason**: Bags no longer reference equipment; shots retain their equipment reference and dial snapshots.
**Migration**: Preserve shot equipment references and remove bag equipment fields.

## ADDED Requirements

### Requirement: Shots retain their equipment and dial snapshots
Shots SHALL retain their equipment reference, recorded grind and RPM independently of bags.

#### Scenario: Load an existing shot
- **WHEN** a shot recorded before the upgrade is opened
- **THEN** its recorded equipment, grind and RPM remain available and unchanged

### Requirement: Equipment lifecycle does not depend on bags
Equipment deletion, merging and identity edits SHALL work without bag equipment references while preserving the existing protection of shots, recipes and package lineage.

#### Scenario: Merge after upgrade
- **WHEN** equipment packages are merged after bag fields are removed
- **THEN** the operation applies its existing shot, recipe and lineage rules without querying a bag equipment field
