# settings-architecture Specification

## Purpose
TBD - created by archiving change split-headers-by-domain. Update Purpose after archive.
## Requirements
### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects. Each domain sub-object SHALL be a standalone `QObject` subclass owning its own `QSettings` instance and containing only the properties, signals, and methods for its domain. `Settings` SHALL own the domain objects, construct them as children, and expose each via a `Q_PROPERTY(QObject* domain READ domainQObject CONSTANT)` accessor (the `QObject*` type is required so QML can resolve via runtime metaObject without including the domain header in `settings.h`). C++ callers SHALL use a typed inline accessor (`SettingsDomain* domain() const`) declared alongside the `Q_PROPERTY`. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods (`sync`, `factoryReset`, cross-domain `connect()` declarations) and SHALL be under 200 lines.

The complete domain set SHALL be: `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`.

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on a single domain's settings
- **THEN** it includes that domain's header (e.g., `settings_brew.h`) and receives a `SettingsBrew*` — not `Settings*`
- **AND** changing any non-Brew domain header does not trigger recompilation of that component

#### Scenario: Settings.h is a thin façade
- **WHEN** all 11 domain splits are complete
- **THEN** `settings.h` contains only sub-object `Q_PROPERTY` accessors, typed inline accessors, cross-domain method declarations, and `sync`/`factoryReset`
- **AND** `settings.h` is under 200 lines
- **AND** `settings.h` contains no `Q_PROPERTY` for any property that has been moved to a sub-object

#### Scenario: Each new domain sub-object is QML-introspectable
- **WHEN** a new `Settings<Domain>` class is added
- **THEN** `main.cpp` calls `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", 1, 0, "Settings<Domain>Type", ...)`
- **AND** QML expressions like `Settings.<domain>.<prop>` resolve to the sub-object's property at runtime, not to `undefined`

#### Scenario: Cross-domain side effects use connect-based wiring
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `setDefaultShotRating` on `SettingsVisualizer` triggers `setDyeEspressoEnjoyment` on `SettingsDye`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

### Requirement: QML Sub-Object Access

All QML code that accesses settings SHALL use the domain sub-object accessor (e.g., `Settings.brew.espressoTemperature`) rather than the flat `Settings` property (e.g., `Settings.espressoTemperature`). `Connections` blocks that target settings properties SHALL target the domain sub-object (`Connections { target: Settings.brew }`). The flat `Settings.X` form is valid only for properties that genuinely remain on `Settings` itself (currently: only the 11 sub-object accessors plus any cross-domain coordinator state).

#### Scenario: QML reads a setting via domain sub-object
- **WHEN** `BrewSettingsPage.qml` reads the espresso temperature
- **THEN** it accesses `Settings.brew.espressoTemperature`
- **AND** the binding updates when `SettingsBrew::espressoTemperatureChanged` fires

#### Scenario: No flat Settings.* access for migrated properties
- **WHEN** the codebase is searched for `Settings\.espressoTemperature` (or any other migrated property) in QML files
- **THEN** zero matches are found
- **AND** every reader uses the domain-prefixed form

#### Scenario: QML Connections target the sub-object
- **WHEN** a QML component listens for changes to a migrated property's signal
- **THEN** it uses `Connections { target: Settings.<domain> }`
- **AND** not `Connections { target: Settings }` — the latter would silently never fire

### Requirement: Narrow Consumer Header Isolation

Each domain-specific C++ consumer (a class that reads only one domain's settings) SHALL include only the domain header, not `settings.h`. Its constructor SHALL accept the domain sub-object pointer instead of `Settings*`. `main.cpp` SHALL pass the sub-object accessor (e.g., `settings.brew()`) at the call site. Wide consumers (classes that touch multiple domains, e.g., `MainController`, `settingsserializer.cpp`) MAY keep `Settings*` and access domains via `settings->domain()->X()`.

#### Scenario: Narrow consumer does not include settings.h
- **WHEN** a domain-specific consumer's header is compiled
- **THEN** the file does not `#include "settings.h"` and does not include any other domain's header
- **AND** changing `settings.h` does not trigger its recompilation

#### Scenario: Settings.h transitive includer count is bounded
- **WHEN** the project is fully migrated
- **THEN** the count of `.cpp` files that transitively include `settings.h` is 10 or fewer
- **AND** measurement comparison with the pre-Tier-1 baseline (39 includers) is documented in the merging PR

### Requirement: Shot History Types Extraction

The data structures defined in `shothistorystorage.h` (`ShotRecord`, `HistoryShotSummary`, `ShotFilter`, `ShotSaveData`, `GrinderContext`, `HistoryPhaseMarker`) SHALL be extracted to `shothistory_types.h`. `shothistorystorage.h` SHALL `#include "shothistory_types.h"`. Components that only store a `ShotHistoryStorage*` pointer and call simple accessors (`databasePath()`, `totalShots()`, `isReady()`) SHALL forward-declare `ShotHistoryStorage` in their headers and include `shothistorystorage.h` only in their `.cpp`.

#### Scenario: Pointer-only consumer avoids full header
- **WHEN** `DatabaseBackupManager` is compiled
- **THEN** it uses `class ShotHistoryStorage;` forward declaration in its header
- **AND** includes `shothistorystorage.h` only in its `.cpp`
- **AND** changing shot history struct definitions does not trigger its recompilation

#### Scenario: Struct consumers include the types header
- **WHEN** `ShotAnalysis` uses `ShotRecord` fields
- **THEN** it includes `shothistory_types.h` (or `shothistorystorage.h` which re-exports it)
- **AND** its compile behaviour is unchanged from before the extraction

### Requirement: Storage Key Stability

Each domain sub-object's `QSettings` operations SHALL use the same key strings the property used before being migrated to the sub-object. No migration of existing user settings is required — opening a separate `QSettings("DecentEspresso", "DE1Qt")` handle in each sub-object provides shared access to the same backing store.

#### Scenario: Existing user settings persist across the migration
- **WHEN** a user upgrades from a build with `Settings::espressoTemperature` to a build with `SettingsBrew::espressoTemperature`
- **THEN** the previously-saved espresso temperature is read correctly on first launch
- **AND** no settings reset or migration step is required

#### Scenario: Key strings match pre-split values
- **WHEN** a property's setter or getter is reviewed in the migrated sub-object
- **THEN** the string passed to `m_settings.value(...)` / `m_settings.setValue(...)` is byte-identical to the pre-migration string in `settings.cpp`

