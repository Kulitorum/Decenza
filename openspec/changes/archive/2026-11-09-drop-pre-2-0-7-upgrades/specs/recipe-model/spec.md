# Spec Delta

## REMOVED Requirements

### Requirement: Legacy absolute yields migrate to yield specs
**Reason**: The migration (34) is below the upgrade floor (schema 41). The import-time conversion served only sources that predate 34, and those are now refused (see `upgrade-floor`).
**Migration**: Install Decenza 2.1.0 first, or restore an old backup there, to convert legacy yields.

### Requirement: Existing recipes migrate to bag links
**Reason**: The migration (29–30) is below the upgrade floor (schema 41). Every database the app upgrades from has already run it.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.
