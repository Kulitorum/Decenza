## MODIFIED Requirements

### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects. Each domain sub-object SHALL be a standalone `QObject` subclass owning its own `QSettings` instance and containing only the properties, signals, and methods for its domain. `Settings` SHALL own the domain objects, construct them as children, and expose each via a `Q_PROPERTY(QObject* domain READ domainQObject CONSTANT)` accessor (the `QObject*` type is required so QML can resolve via runtime metaObject without including the domain header in `settings.h`). C++ callers SHALL use a typed inline accessor (`SettingsDomain* domain() const`) declared alongside the `Q_PROPERTY`. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods (`sync`, `factoryReset`, cross-domain `connect()` declarations) and SHALL be under 200 lines.

The complete domain set SHALL be: `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`, `SettingsCalibration`.

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on a single domain's settings
- **THEN** it includes that domain's header (e.g., `settings_calibration.h`) and receives a `SettingsCalibration*` — not `Settings*`
- **AND** changing any non-Calibration domain header does not trigger recompilation of that component

#### Scenario: Settings.h is a thin façade
- **WHEN** all 12 domain splits are complete
- **THEN** `settings.h` contains only sub-object `Q_PROPERTY` accessors, typed inline accessors, cross-domain method declarations, and `sync`/`factoryReset`
- **AND** `settings.h` is under 200 lines
- **AND** `settings.h` contains no `Q_PROPERTY` for any property that has been moved to a sub-object

#### Scenario: Each new domain sub-object is QML-introspectable
- **WHEN** a new `Settings<Domain>` class is added
- **THEN** `main.cpp` calls `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", 1, 0, "Settings<Domain>Type", ...)`
- **AND** QML expressions like `Settings.<domain>.<prop>` resolve to the sub-object's property at runtime, not to `undefined`

#### Scenario: Cross-domain side effects use connect-based wiring
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `resetSawLearning` on `SettingsCalibration` must reset hot-water SAW offset state on `SettingsBrew`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

#### Scenario: Cross-domain wiring SHALL NOT mirror a setting onto per-shot state
- **WHEN** a proposed cross-domain wiring would copy a configured setting into a field that is snapshotted onto a shot at save time
- **THEN** the wiring SHALL NOT be established
- **AND** the value SHALL be written to the shot record at the point a person supplies it instead
- **AND** the reason SHALL be understood as concrete rather than stylistic: the removed `setDefaultShotRating` → `setDyeEspressoEnjoyment` wiring is what made a deleted setting keep rating shots, because the mirrored field outlived the setting that fed it

## ADDED Requirements

### Requirement: Removing a setting SHALL remove its stored key

When a setting is removed, deleting its property and accessors SHALL NOT be
considered sufficient. Every key the removed feature wrote SHALL also be evicted
from the settings store, including keys written by fields the setting fed, so an
upgraded store carries no orphaned value from the removed feature.

Eviction SHALL be idempotent — a removal of an absent key is a no-op — so it can
run unconditionally on construction without a version counter or migration
framework.

This requirement exists because a store is shared mutable state with an unbounded
lifetime: an orphaned value is only inert for as long as nothing reads it, and the
next reader is written by someone who never knew the feature existed.

#### Scenario: Removed setting leaves no key behind

- **GIVEN** a settings store written by a build that had the removed feature
- **WHEN** the user upgrades to a build where the feature is gone
- **THEN** the store SHALL contain no key that the removed feature wrote
- **AND** a subsequent launch SHALL perform no further eviction work
