# Spec Delta

## REMOVED Requirements

### Requirement: Legacy recipe grind survives import without bag grind storage
**Reason**: It serves only source databases below schema 30. Restores and transfers now refuse any source below the upgrade floor (schema 41), so that source never reaches the recipe importer (see `upgrade-floor`).
**Migration**: Restore or transfer such data into Decenza 2.1.0 first.
