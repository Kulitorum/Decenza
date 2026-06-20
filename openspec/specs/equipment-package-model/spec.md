# equipment-package-model Specification

## Purpose
TBD - created by archiving change add-equipment-packages. Update Purpose after archive.
## Requirements
### Requirement: EquipmentPackage and EquipmentItem data model
The system SHALL define an `EquipmentPackage` value type (a container) and an `EquipmentItem` value type (a typed component), where one package owns one or more items.

`EquipmentPackage` fields:
- Identity: `id` (int, DB primary key), `name` (nullable — defaults for display to the grinder item's "{brand} {model}")
- Lifecycle: `inInventory` (bool, default true), `lastUsed` (nullable timestamp), `createdAt`
- Grinder-scoped dial memory: `lastGrindSetting` (nullable string), `lastRpm` (nullable int)

`EquipmentItem` fields:
- `id` (int, DB primary key), `packageId` (int, FK → `equipment_packages.id`)
- `kind` (string enum — `"grinder"` today)
- `brand` (string), `model` (string)
- `attrs_json` (JSON blob, kind-specific) — for `kind="grinder"`: `burrs` (string), `rpmCapable` (bool)

#### Scenario: A package owns one grinder today
- **WHEN** an equipment package is created
- **THEN** it SHALL contain exactly one `equipment_item` with `kind = "grinder"`
- **AND** that item SHALL carry `brand`, `model`, and an `attrs_json` with `burrs` and `rpmCapable`

#### Scenario: Adding a new component kind requires no schema migration
- **WHEN** a future component kind (e.g. `"basket"`) is introduced
- **THEN** it SHALL be stored as a new `equipment_items` row with that `kind` and a kind-specific `attrs_json`
- **AND** no change to the `equipment_packages` or `equipment_items` table schema SHALL be required

### Requirement: equipment_packages and equipment_items database tables
The system SHALL create `equipment_packages` and `equipment_items` SQLite tables in **migration 22** in `src/history/shothistorystorage.cpp` (current schema version is 21). DB access SHALL follow the `withTempDb()` background-thread pattern via a new `EquipmentStorage` class modeled on `CoffeeBagStorage`.

#### Scenario: Tables created on upgrade
- **WHEN** the app launches after upgrade at schema version < 22
- **THEN** the `equipment_packages` and `equipment_items` tables SHALL be created
- **AND** the schema version SHALL be set to 22 only after the migration transaction commits

### Requirement: coffee_bags and shots reference a package via equipment_id
Migration 22 SHALL add a nullable `equipment_id` INTEGER foreign-key column to both `coffee_bags` and `shots`, pointing at `equipment_packages.id`, and SHALL drop the `grinder_brand`, `grinder_model`, and `grinder_burrs` columns from both tables. Both tables SHALL **retain** `grinder_setting` and SHALL **add** a nullable `rpm` INTEGER column.

#### Scenario: Grinder identity resolves through the pointer
- **WHEN** code needs a bag's or shot's grinder brand/model/burrs
- **THEN** it SHALL resolve them by following `equipment_id` to the package's grinder item
- **AND** no grinder identity strings SHALL be stored on the `coffee_bags` or `shots` rows

#### Scenario: Dial-in stays on the row
- **WHEN** a shot is saved
- **THEN** its `grinder_setting` and `rpm` SHALL be snapshotted on the `shots` row (not on the package)

### Requirement: Reference semantics with soft-delete
Equipment packages SHALL use reference semantics: editing a package's fields SHALL be reflected by every bag and shot pointing at it. Removing a package from use SHALL set `inInventory = 0` and SHALL NOT delete the row, so historical bags and shots continue to resolve.

#### Scenario: Editing a package updates history
- **WHEN** a user edits a package's grinder burrs
- **THEN** every bag and shot pointing at that package SHALL resolve to the edited burrs

#### Scenario: Replacing a grinder
- **WHEN** a user replaces their grinder
- **THEN** they SHALL create a new package and the old package SHALL be soft-deleted (`inInventory = 0`)
- **AND** shots taken with the old grinder SHALL still resolve to the old package's identity

### Requirement: rpmCapable is derived, not user-configured
When a package's grinder item is created or its brand/model is edited, the system SHALL set `rpmCapable` from the `GrinderAliases` registry: a grinder matched in the registry SHALL use its `variableRpm` flag; a grinder not in the registry SHALL be treated as `rpmCapable = true`. The registry SHALL remain hardcoded and not user-editable.

#### Scenario: Known non-variable grinder
- **WHEN** a package is created for a registry grinder whose `variableRpm` is false
- **THEN** `rpmCapable` SHALL be false and the RPM dial-in SHALL be hidden in Brew Settings

#### Scenario: Custom grinder not in the registry
- **WHEN** a user types a grinder brand/model that matches no registry alias
- **THEN** `rpmCapable` SHALL be true and the RPM dial-in SHALL be shown

#### Scenario: rpmCapable re-derives on edit
- **WHEN** a package's grinder brand/model is changed
- **THEN** `rpmCapable` SHALL be re-derived from the registry for the new identity

### Requirement: Grinder-scoped dial memory and dual write-through
Switching the active bag's equipment package SHALL apply that package's `lastGrindSetting`/`lastRpm` to the active bag's grind setting and rpm (and thereby Brew Settings), never leaving them blank but leaving them editable. Editing the grind setting or rpm SHALL write through to **both** the active bag and the active package's `lastGrindSetting`/`lastRpm`.

#### Scenario: Switch equipment pre-fills the dial
- **WHEN** the user switches the active bag to a different equipment package
- **THEN** the active bag's grind setting and rpm SHALL be set to that package's `lastGrindSetting`/`lastRpm`
- **AND** the values SHALL remain editable

#### Scenario: Editing the dial updates both memories
- **WHEN** the user edits the grind setting or rpm in Brew Settings
- **THEN** the new value SHALL be written to the active bag AND to the active package's last-dial fields

### Requirement: SettingsDye exposes resolved grinder identity and equipment selection
`SettingsDye` SHALL expose `dyeGrinderBrand`, `dyeGrinderModel`, and `dyeGrinderBurrs` as **read-only** values resolved through the active bag's `equipment_id`. It SHALL expose `dyeGrinderSetting` and a new `dyeGrinderRpm` as writable dial-in values (writing through to bag and active package per the dual write-through rule), and an `activeEquipmentId` for selecting/switching the package.

#### Scenario: Read resolves via the package
- **WHEN** `dyeGrinderModel` is read
- **THEN** it SHALL return the model of the active bag's package grinder item, or empty when no package is linked

### Requirement: Migration 22 splits combined grind+rpm settings
Migration 22 SHALL perform a best-effort, marker-gated split of every existing `grinder_setting` on `coffee_bags` and `shots`: a trailing token matching `(\d+)\s*rpm` (case-insensitive) SHALL move the number into `rpm` and leave the remainder (trimmed) in `grinder_setting`. A setting with no explicit `rpm` marker SHALL be left exactly as-is with `rpm` null.

#### Scenario: Annotated setting is split
- **WHEN** a row's `grinder_setting` is `"24 1400rpm"`
- **THEN** after migration `grinder_setting` SHALL be `"24"` and `rpm` SHALL be `1400`

#### Scenario: Compound or non-rpm setting is preserved
- **WHEN** a row's `grinder_setting` is `"1+4"` or `"24 clicks"`
- **THEN** `grinder_setting` SHALL be unchanged and `rpm` SHALL be null

### Requirement: Migration 22 creates packages and links rows
Migration 22 SHALL create equipment packages keyed on grinder identity `(brand, model, burrs)` only — grind setting and rpm SHALL NOT affect package creation. It SHALL create one default package from the user's current grinder settings, plus one package per remaining distinct grinder identity found across `coffee_bags` and `shots`, and SHALL set each row's `equipment_id` to its matching package.

#### Scenario: One grinder, many grind settings
- **WHEN** a user has a single grinder identity across all bags/shots but many different grind settings
- **THEN** migration SHALL create exactly one package
- **AND** every bag and shot SHALL link to it, retaining its own (split) grind setting and rpm

#### Scenario: Distinct historical grinder
- **WHEN** some historical bag or shot has a grinder identity different from the current settings
- **THEN** a separate package SHALL be created for that identity and those rows linked to it

#### Scenario: Empty grinder identity
- **WHEN** a bag or shot has empty grinder brand/model/burrs
- **THEN** its `equipment_id` SHALL be NULL and no package SHALL be created for it

#### Scenario: Package last-dial seeding
- **WHEN** a package is created during migration
- **THEN** the default package's `lastGrindSetting`/`lastRpm` SHALL seed from the current settings' (split) values
- **AND** a per-grinder package's `lastGrindSetting`/`lastRpm` SHALL seed from that grinder's most-recent shot

### Requirement: Equipment survives backup restore and device-to-device transfer
The DB import path (`ShotHistoryStorage::importDatabaseStatic`) SHALL migrate `equipment_packages` and `equipment_items` rows and remap `coffee_bags.equipment_id` and `shots.equipment_id` to the new package ids (alongside the existing bag-id remap). `SettingsSerializer` SHALL exclude `dye/activeEquipmentId` from export.

#### Scenario: Restoring a backup with equipment
- **WHEN** a backup containing equipment tables is restored
- **THEN** all packages and items SHALL be imported with new ids
- **AND** imported bags' and shots' `equipment_id` SHALL point at the corresponding imported packages

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

