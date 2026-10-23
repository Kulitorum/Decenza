## ADDED Requirements

### Requirement: The Equipment page SHALL show archived packages and restore them

The used-package removal action SHALL be labelled **Archive**; delete SHALL remain
available only for a package no shot references. The Equipment page SHALL offer a
"Show archived (N)" toggle, shown when archived packages exist, that lists archived
packages as dimmed cards with a **Restore** action. Forked packages SHALL NOT be listed.

#### Scenario: Restoring an archived package

- **GIVEN** an archived package
- **WHEN** the user shows archived packages and taps Restore on it
- **THEN** it returns to the inventory list with its history

#### Scenario: Archived packages stay out of the way

- **WHEN** archived packages are hidden (the default)
- **THEN** only in-inventory packages are listed and the toggle shows the archived count
