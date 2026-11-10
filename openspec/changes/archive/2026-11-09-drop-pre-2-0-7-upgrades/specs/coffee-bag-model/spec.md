# Spec Delta

## ADDED Requirements

### Requirement: Bags survive restore and transfer with remapped ids
The DB import path (`ShotHistoryStorage::importDatabaseStatic`) SHALL migrate `coffee_bags` rows and remap `shots.bag_id` to the new bag row ids (shot ids are remapped on import; bag ids must follow). `dye/activeBagId` SHALL be excluded from settings export/import.

#### Scenario: Restoring a backup with bags
- **WHEN** a backup containing a `coffee_bags` table is restored
- **THEN** all bags SHALL be imported with new row ids
- **AND** imported shots' `bag_id` values SHALL point at the corresponding imported bags

## REMOVED Requirements

### Requirement: Bags survive backup restore and device-to-device transfer
**Reason**: Its legacy `beans.presets` import and its migration-19 backfill serve only pre-2.0.7 data, which is now refused or ignored (see `upgrade-floor`).
**Migration**: None. Replaced by "Bags survive restore and transfer with remapped ids".

### Requirement: Stored bags carrying a borrowed canonical record are unlinked by migration
**Reason**: It is a step in migrations 37–38, below the upgrade floor (schema 41). Every database the app upgrades from has already run it.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.

### Requirement: The unlink does not depend on the repair queue
**Reason**: It qualifies the removed unlink migration.
**Migration**: None.
