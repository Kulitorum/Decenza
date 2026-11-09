# Spec Delta

## REMOVED Requirements

### Requirement: Migration 22 splits combined grind+rpm settings
**Reason**: Migration 22 is below the upgrade floor (schema 41). Every database the app upgrades from has already run it.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.

### Requirement: Migration 22 creates packages and links rows
**Reason**: Migration 22 is below the upgrade floor (schema 41). Every database the app upgrades from has already run it.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.

### Requirement: Packages split by a pre-enrichment fork SHALL be healed once on upgrade
**Reason**: The heal runs in migrations 35–36, below the upgrade floor (schema 41). Every database the app upgrades from has already been healed.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.

### Requirement: Heal runs in the migration transaction
**Reason**: It qualifies the removed heal.
**Migration**: None.
