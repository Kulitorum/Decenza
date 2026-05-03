# Spec Delta: settings-architecture

## MODIFIED Requirements

### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects. Each domain sub-object SHALL be a standalone `QObject` subclass owning its own `QSettings` instance and containing only the properties, signals, and methods for its domain. `Settings` SHALL own the domain objects, construct them as children, and expose each via a `Q_PROPERTY(QObject* domain READ domainQObject CONSTANT)` accessor (the `QObject*` type is required so QML can resolve via runtime metaObject without including the domain header in `settings.h`). C++ callers SHALL use a typed inline accessor (`SettingsDomain* domain() const`) declared alongside the `Q_PROPERTY`. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods (`sync`, `factoryReset`, cross-domain `connect()` declarations) and SHALL be under 200 lines.

The complete domain set SHALL be: `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp`, `SettingsCalibration`, `SettingsConnections`.

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on a single domain's settings
- **THEN** it includes that domain's header (e.g., `settings_calibration.h` or `settings_connections.h`) and receives a `SettingsCalibration*` or `SettingsConnections*` — not `Settings*`
- **AND** changing any non-target-domain header does not trigger recompilation of that component

#### Scenario: Settings.h is a thin façade
- **WHEN** all 13 domain splits are complete
- **THEN** `settings.h` contains only sub-object `Q_PROPERTY` accessors, typed inline accessors, cross-domain method declarations, and `sync`/`factoryReset`
- **AND** `settings.h` is under 200 lines
- **AND** `settings.h` contains no `Q_PROPERTY` for any property that has been moved to a sub-object

#### Scenario: Each new domain sub-object is QML-introspectable
- **WHEN** a new `Settings<Domain>` class is added
- **THEN** `main.cpp` calls `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", 1, 0, "Settings<Domain>Type", ...)`
- **AND** QML expressions like `Settings.<domain>.<prop>` resolve to the sub-object's property at runtime, not to `undefined`

#### Scenario: Cross-domain side effects use connect-based wiring
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `resetSawLearning` on `SettingsCalibration` must reset hot-water SAW offset state on `SettingsBrew`, or `setDefaultShotRating` on `SettingsVisualizer` triggers `setDyeEspressoEnjoyment` on `SettingsDye`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

## ADDED Requirements

### Requirement: Connections Domain Surface

The `SettingsConnections` domain sub-object SHALL own all device-pairing transport state that is intentionally persisted across app launches. The initial surface comprises the scale Wi-Fi pairing map and its accessors; future device-pairing state (e.g. DE1 last-known-MAC) MAY be migrated to this domain in subsequent changes.

The initial surface comprises: `scaleWifiPairings` (`Q_PROPERTY` of type `QVariantMap`), `setScaleWifiPairings` (setter), `scaleWifiPairingsChanged` (notify signal), and the `Q_INVOKABLE` helpers `setScaleWifiPairing(QString mac, QString ip, int port)`, `clearScaleWifiPairing(QString mac)`, and `scaleWifiPairing(QString mac)`.

#### Scenario: Connections surface lives on the sub-object
- **WHEN** a developer searches `src/core/settings.h` for `scaleWifiPairings`, `setScaleWifiPairing`, `clearScaleWifiPairing`, or `scaleWifiPairing`
- **THEN** zero matches are found in `settings.h`
- **AND** every listed name appears only in `settings_connections.h` and `settings_connections.cpp`

#### Scenario: QML access is via the sub-object
- **WHEN** `SettingsConnectionsTab.qml` reads or writes a scale Wi-Fi pairing
- **THEN** it accesses `Settings.connections.scaleWifiPairings` and the `Q_INVOKABLE` helpers via `Settings.connections.setScaleWifiPairing(...)` etc.
- **AND** never via the flat `Settings.scaleWifiPairings` form (which would resolve to `undefined`)

#### Scenario: Storage key namespace is isolated
- **WHEN** `SettingsConnections` reads or writes via its `QSettings` instance
- **THEN** all keys SHALL be under the `connections/` prefix (e.g. `connections/scaleWifiPairings/<mac>/ip`)
- **AND** SHALL NOT collide with any existing key namespace owned by another domain
