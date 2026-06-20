## ADDED Requirements

### Requirement: The equipment card SHALL display the package's basket
`EquipmentCard` SHALL show the package's basket on its own line (below the grinder
title / burrs), using the same typography and secondary-color convention as the
burrs line. When a package has no basket, the basket line SHALL be omitted (not
shown blank). The grinder SHALL remain the card title; the basket SHALL be a
subordinate line.

#### Scenario: Card with a basket
- **WHEN** a package has a basket item
- **THEN** the card SHALL render a basket line showing the basket brand + model

#### Scenario: Card with no basket
- **WHEN** a package has no basket item
- **THEN** the card SHALL omit the basket line entirely

### Requirement: The equipment info dialog SHALL include the basket
`EquipmentInfoDialog` SHALL add a basket `InfoRow` (label + value) showing the
basket identity, placed with the other equipment rows. When no basket is set, the
row SHALL be omitted.

#### Scenario: Info dialog shows the basket
- **WHEN** the info dialog opens for a package with a basket
- **THEN** it SHALL include a basket row showing the basket brand + model
