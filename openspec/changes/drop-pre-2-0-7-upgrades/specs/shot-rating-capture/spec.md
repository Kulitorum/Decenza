# Spec Delta

## REMOVED Requirements

### Requirement: Migration 16 SHALL reset inferred ratings to unrated
**Reason**: Migration 16 is below the upgrade floor (schema 41). Every database the app upgrades from has already run it, and no `enjoyment_source` column remains to read.
**Migration**: Install Decenza 2.1.0 first to bring an older database past this step.
