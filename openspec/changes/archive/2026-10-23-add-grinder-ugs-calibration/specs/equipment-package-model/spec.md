## ADDED Requirements

### Requirement: An archived package SHALL be one the user removed, distinct from a forked one

A package with `inInventory = 0` and no `supersededBy` SHALL be treated as **archived**:
the user took it out of inventory and it can be restored. A package with
`supersededBy` set was retired by a copy-on-write fork or merge and SHALL NOT be
treated as archived. Neither kind SHALL ever be deleted while a shot references it.

#### Scenario: Removing a used package archives it

- **WHEN** the user archives a package that has shots
- **THEN** it leaves the inventory with `inInventory = 0`, keeps its row, and has no `supersededBy`

### Requirement: Recreating an archived package's gear SHALL restore it instead of duplicating it

When a package is created whose full identity (grinder brand, model and burrs, basket
brand and model, puck prep) matches an archived package and no in-inventory package,
the archived package SHALL be returned to inventory and reused; no new row SHALL be
created. An identity edit that matches an archived package SHALL merge into it and
return it to inventory, as an edit matching an in-inventory package already merges.

#### Scenario: Creating archived gear restores it

- **GIVEN** an archived Niche Zero package
- **WHEN** the user creates a package with the same grinder, burrs, basket and puck prep
- **THEN** the archived package is back in inventory with its own id and history
- **AND** no new package exists

#### Scenario: A forked package is not revived

- **GIVEN** a package retired by a fork (`supersededBy` set)
- **WHEN** a package is created with its old identity
- **THEN** a new package is created and the retired one stays retired

### Requirement: Restoring a package SHALL be refused only for a forked package

Restoring SHALL return an archived package to inventory unchanged, including its
name, even when another in-inventory package has the same name: the unique-name rule
applies to renames, and derived names repeat by design. Restoring SHALL be refused for
a forked package.

#### Scenario: Restore beside a same-named package

- **GIVEN** an archived package and an in-inventory package both named "Niche Zero"
- **WHEN** the user restores the archived package
- **THEN** it returns to inventory with its name unchanged
