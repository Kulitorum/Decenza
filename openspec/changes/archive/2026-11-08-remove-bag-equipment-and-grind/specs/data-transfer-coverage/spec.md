# Spec Delta

## ADDED Requirements

### Requirement: Legacy recipe grind survives import without bag grind storage
Import SHALL preserve shot history and other bag data without restoring bag equipment or grind. For a newly imported legacy recipe that inherited grind, it SHALL recover the grind and RPM from source data before discarding bag fields; current-era empty grinds and matched destination recipes SHALL remain untouched.

#### Scenario: Source recipe with a bag ID
- **WHEN** an old inherited recipe and its linked bag are imported
- **THEN** the recipe adopts the source bag's grind and RPM and the imported bag stores neither

#### Scenario: Source recipe predates bag links
- **WHEN** an inherited recipe predates bag IDs
- **THEN** source open bags are resolved using the existing legacy bean matching and ordering rules before its grind is recovered

#### Scenario: No source match
- **WHEN** no usable source bag matches the legacy recipe
- **THEN** the imported recipe remains grind-less

#### Scenario: Merge preserves destination
- **WHEN** an imported recipe matches an existing destination recipe or a current-era recipe explicitly has no grind
- **THEN** no legacy backfill overwrites its empty grind
