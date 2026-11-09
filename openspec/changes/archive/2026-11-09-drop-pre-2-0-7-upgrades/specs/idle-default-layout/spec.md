# Spec Delta

## REMOVED Requirements

### Requirement: Injection migrations do not distort the new default
**Reason**: The Equipment and Recipes button injections it constrains are removed. They fired only on crossing schemas 22 and 25, which no database at or above the upgrade floor (schema 41) can do.
**Migration**: None.
