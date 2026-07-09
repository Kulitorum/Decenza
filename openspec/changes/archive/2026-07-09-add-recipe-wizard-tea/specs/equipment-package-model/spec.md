# equipment-package-model Specification (delta)

## MODIFIED Requirements

### Requirement: EquipmentPackage and EquipmentItem data model
The system SHALL define an `EquipmentPackage` value type (a container) and an `EquipmentItem` value type (a typed component), where one package owns one or more items.

`EquipmentPackage` fields:
- Identity: `id` (int, DB primary key), `name` (nullable — defaults for display to the grinder item's "{brand} {model}", or the basket item's "{brand} {model}" for grinder-less packages)
- Lifecycle: `inInventory` (bool, default true), `lastUsed` (nullable timestamp), `createdAt`
- Grinder-scoped dial memory: `lastGrindSetting` (nullable string), `lastRpm` (nullable int)

`EquipmentItem` fields:
- `id` (int, DB primary key), `packageId` (int, FK → `equipment_packages.id`)
- `kind` (string enum — `"grinder"` today)
- `brand` (string), `model` (string)
- `attrs_json` (JSON blob, kind-specific) — for `kind="grinder"`: `burrs` (string), `rpmCapable` (bool)

A package MAY be grinder-less (e.g. a tea setup that is basket-only): create paths SHALL accept a package with no grinder item, and consumers SHALL tolerate an invalid grinder item (grind/rpm surfaces and dial memory simply absent), mirroring the existing optional-basket handling.

#### Scenario: A package owns at most one grinder
- **WHEN** an equipment package is created with a grinder
- **THEN** it SHALL contain exactly one `equipment_item` with `kind = "grinder"`
- **AND** that item SHALL carry `brand`, `model`, and an `attrs_json` with `burrs` and `rpmCapable`

#### Scenario: Basket-only package
- **WHEN** a package is created with a basket and no grinder
- **THEN** it saves, displays under the basket's brand+model, exposes no grind/rpm surfaces, and can be selected by recipes and bags

#### Scenario: Adding a new component kind requires no schema migration
- **WHEN** a future component kind (e.g. `"basket"`) is introduced
- **THEN** it SHALL be stored as a new `equipment_items` row with that `kind` and a kind-specific `attrs_json`
- **AND** no change to the `equipment_packages` or `equipment_items` table schema SHALL be required
