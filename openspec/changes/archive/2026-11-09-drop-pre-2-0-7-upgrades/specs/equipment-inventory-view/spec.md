# Spec Delta

## REMOVED Requirements

### Requirement: Idempotent layout-injection migration for existing users
**Reason**: The injection fires only when a database crosses schema 22. Every database the app upgrades from is at 41 or above, so it can never fire again.
**Migration**: None. Users add the Equipment button from the layout editor.
