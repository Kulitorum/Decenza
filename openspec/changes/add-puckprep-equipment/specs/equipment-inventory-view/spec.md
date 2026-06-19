## ADDED Requirements

### Requirement: The equipment card SHALL display the package's puck prep
`EquipmentCard` SHALL show the package's puck prep on its own line (a compact
summary of the set flags, e.g. "WDT · Shaker"), below the existing grinder / basket
lines. When a package has no puck prep, the line SHALL be omitted (not shown blank).

#### Scenario: Card with puck prep
- **WHEN** a package has a puck-prep item with flags set
- **THEN** the card SHALL render a puck-prep line summarizing the set flags

#### Scenario: Card with no puck prep
- **WHEN** a package has no puck-prep item
- **THEN** the card SHALL omit the puck-prep line entirely

### Requirement: The equipment info dialog SHALL include the puck prep
`EquipmentInfoDialog` SHALL include the package's puck prep — the set flags and the
derived `distribution` — placed after the grinder / basket / dial rows. Rows SHALL
self-hide when the package has no puck prep.

#### Scenario: Info dialog shows puck prep
- **WHEN** the info dialog opens for a package with puck prep
- **THEN** it SHALL show the set flags and the derived distribution
