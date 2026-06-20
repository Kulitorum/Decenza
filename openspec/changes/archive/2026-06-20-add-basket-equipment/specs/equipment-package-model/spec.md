## ADDED Requirements

### Requirement: A package MAY own an optional basket component
An equipment package SHALL be able to own at most one `equipment_items` row of
`kind="basket"` (`brand`, `model`), in addition to its grinder item. A package with
no basket item SHALL remain valid (grinder-only packages, including all packages
that predate this change, are unaffected). Adding a basket SHALL NOT require a DB
schema migration — it is a new row of an existing kind.

#### Scenario: Package created with a grinder and a basket
- **WHEN** a package is created with a grinder identity and a basket identity
- **THEN** the package SHALL own one `kind="grinder"` item and one `kind="basket"` item
- **AND** the package view SHALL resolve and expose both

#### Scenario: Package created with no basket
- **WHEN** a package is created with a grinder identity and no basket
- **THEN** the package SHALL own only the grinder item
- **AND** the package view SHALL expose an empty/absent basket without error

### Requirement: Basket specs SHALL be derived from the registry, not stored
The basket item SHALL persist only `kind`, `brand`, and `model` (empty `attrs`).
Its physical characteristics — wall profile, relative flow class, precision flag,
dose range, material — SHALL be derived at read time from
`BasketAliases::findEntry(brand, model)`, mirroring how the grinder item derives
`rpmCapable`. A basket whose identity does not match the registry (a custom basket)
SHALL resolve with unknown specs and SHALL NOT block creation.

#### Scenario: Registry basket resolves specs
- **WHEN** a package's basket item is `{brand: "VST", model: "18g"}` and that entry exists in the registry
- **THEN** resolving the package SHALL expose the registry's wall profile, relative flow, precision, dose range, and material

#### Scenario: Custom basket resolves with unknown specs
- **WHEN** a package's basket item does not match any registry entry
- **THEN** resolving the package SHALL expose the basket brand/model with specs marked unknown/absent rather than fabricated

### Requirement: Package identity SHALL include the basket
The package identity used for dedup and copy-on-write SHALL be the tuple of the
grinder identity (`brand/model/burrs`) **and** the basket identity (`brand/model`),
where "no basket" is a distinct identity value. Two packages that share a grinder
but differ in basket (or in basket-vs-no-basket) SHALL be distinct packages.

#### Scenario: Switching basket on a used package forks
- **WHEN** a package that has recorded shots has its basket changed to a different basket
- **THEN** a new package SHALL be forked under copy-on-write (the old package preserved, `supersededBy` set), exactly as for a grinder identity edit

#### Scenario: Switching back to a prior combo dedups
- **WHEN** the user returns to a previously used grinder+basket combination
- **THEN** the existing matching package SHALL be selected rather than a duplicate created

#### Scenario: Editing basket on an unused package edits in place
- **WHEN** a package with no recorded shots has its basket changed
- **THEN** the package SHALL be edited in place (same id, no fork)

### Requirement: SettingsDye SHALL bridge the basket registry and resolve the active basket
`SettingsDye` SHALL expose `knownBasketBrands()` and `knownBasketModels(brand)`
(registry-backed, plus distinct values from history where applicable) for the
picker, and SHALL resolve the active package's basket identity for display. The
basket identity SHALL be carried through create/update/switch alongside the grinder
identity.

#### Scenario: Basket suggestions sourced from the registry
- **WHEN** the picker requests basket brands, then models for a chosen brand
- **THEN** `knownBasketBrands()` SHALL return the registry's sorted unique brands
- **AND** `knownBasketModels(brand)` SHALL return that brand's models

### Requirement: Device import SHALL carry basket items
Device-to-device equipment import SHALL copy `kind="basket"` items alongside grinder
items when importing packages, preserving the package's basket identity in the
destination.

#### Scenario: Imported package retains its basket
- **WHEN** a package with a basket item is imported from a source database
- **THEN** the destination package SHALL own an equivalent basket item
