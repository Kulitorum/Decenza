# Spec Delta

## MODIFIED Requirements

### Requirement: Recipes link a specific bag
A recipe SHALL link a specific bag via a `bag_id` column, riding transfer and backup import with id remapping like `equipment_id`. Its bean identity (Bean Base canonical id, roaster, coffee) SHALL be kept as a display fallback and as the key for automatic relinking. Activation SHALL use the linked bag directly, never most-recently-used resolution. A recipe MAY have no bag.

#### Scenario: Two open bags of the same bean
- **WHEN** two recipes link two different open bags of the same bean and each is activated in turn
- **THEN** each activation selects exactly its own linked bag and applies its own grind

#### Scenario: Bean-less recipe unaffected
- **WHEN** a recipe with no bag link is activated
- **THEN** the active bag is unchanged and recipe-local grind applies, exactly as before

## REMOVED Requirements

### Requirement: Recipe-owned grind
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Recipes own grind independently of bags.

### Requirement: New-recipe grind defaults from the bag, once
**Reason**: Bags no longer store grind.
**Migration**: Interactive creation defaults from matching shot history; explicitly supplied or empty grind remains authoritative.

## ADDED Requirements

### Requirement: Recipes own grind independently of bags
Grind SHALL always live on the recipe. Every recipe of a grind-bearing drink type, linked to a bag or not, SHALL store its own `grindPinned` (opaque text) and optional `rpmPinned`. Grind-less drink types (`tea` and `tea_hotwater`) SHALL store neither. A recipe's grind SHALL never be read from its bag at activation. Editing grind on an active recipe SHALL write to that recipe, not to its bag.

#### Scenario: Editing grind updates the active recipe, not the bag
- **WHEN** a recipe is active and the user edits its grind or rpm
- **THEN** that recipe's own `grindPinned`/`rpmPinned` change immediately
- **AND** no grind or RPM is written to the bag

#### Scenario: Sibling recipes on the same bag are independent
- **WHEN** two recipes are linked to the same bag and one recipe's grind is edited
- **THEN** the other recipe's own `grindPinned`/`rpmPinned` is unchanged, and no recipe reads grind from the bag

#### Scenario: A live grind change outside a recipe does not alter recipes
- **WHEN** the live grind is adjusted outside a recipe
- **THEN** it does not change any recipe grind or a bag grind field

### Requirement: Recipe creation never inherits a bag grind
A newly created recipe SHALL own its grind and RPM and SHALL NOT adopt them from a bag. Explicitly provided values, including an empty grind, SHALL be retained.

#### Scenario: Explicit empty grind
- **WHEN** recipe creation supplies an empty grind
- **THEN** the recipe stores no grind and does not inherit one

#### Scenario: Historical shot promotion
- **WHEN** a shot is promoted to a recipe
- **THEN** its recorded grind and RPM are used
