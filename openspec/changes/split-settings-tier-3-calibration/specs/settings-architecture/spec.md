# settings-architecture spec delta

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
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `resetSawLearning` on `SettingsCalibration` must reset hot-water SAW offset state on `SettingsBrew`, or `setDefaultShotRating` on `SettingsVisualizer` triggers `setDyeEspressoEnjoyment` on `SettingsDye`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

### Requirement: QML Sub-Object Access

All QML code that accesses settings SHALL use the domain sub-object accessor (e.g., `Settings.calibration.sawLearnedLag`) rather than the flat `Settings` property (e.g., `Settings.sawLearnedLag`). `Connections` blocks that target settings properties SHALL target the domain sub-object (`Connections { target: Settings.calibration }`). The flat `Settings.X` form is valid only for properties that genuinely remain on `Settings` itself (currently: only the 12 sub-object accessors plus any cross-domain coordinator state).

#### Scenario: QML reads a setting via domain sub-object
- **WHEN** `SettingsCalibrationTab.qml` reads the SAW learned lag
- **THEN** it accesses `Settings.calibration.sawLearnedLag`
- **AND** the binding updates when `SettingsCalibration::sawLearnedLagChanged` fires

#### Scenario: No flat Settings.* access for migrated properties
- **WHEN** the codebase is searched for `Settings\.sawLearnedLag` (or any other migrated property — `profileFlowCalibration`, `autoFlowCalibration`, `flowCalibrationMultiplier`, `sawModelSource`, etc.) in QML files
- **THEN** zero matches are found
- **AND** every reader uses the domain-prefixed form

#### Scenario: QML Connections target the sub-object
- **WHEN** a QML component listens for changes to a migrated calibration property's signal
- **THEN** it uses `Connections { target: Settings.calibration }`
- **AND** not `Connections { target: Settings }` — the latter would silently never fire

### Requirement: Storage Key Stability

Each domain sub-object's `QSettings` operations SHALL use the same key strings the property used before being migrated to the sub-object. No migration of existing user settings is required — opening a separate `QSettings("DecentEspresso", "DE1Qt")` handle in each sub-object provides shared access to the same backing store.

#### Scenario: Existing user settings persist across the migration
- **WHEN** a user upgrades from a build with `Settings::sawLearnedLag` to a build with `SettingsCalibration::sawLearnedLag`
- **THEN** the previously-saved SAW learning history (`saw/learningHistory`, `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/<scale>`) is read correctly on first launch
- **AND** the previously-saved flow calibration values (`calibration/flowMultiplier`, `calibration/autoFlowCalibration`, `calibration/perProfileFlow`, `calibration/flowCalBatch`) are read correctly on first launch
- **AND** no settings reset or migration step is required

#### Scenario: Key strings match pre-split values
- **WHEN** a property's setter or getter is reviewed in `SettingsCalibration`
- **THEN** the string passed to `m_settings.value(...)` / `m_settings.setValue(...)` is byte-identical to the pre-migration string in `settings.cpp`

## ADDED Requirements

### Requirement: Calibration Domain Surface

The `SettingsCalibration` domain sub-object SHALL own the auto flow calibration surface and the SAW (stop-at-weight) learning surface in their entirety. No calibration or SAW-related property, invokable, signal, cache, or static helper SHALL remain on `Settings` itself after Tier 3 lands.

The auto flow calibration surface comprises: `flowCalibrationMultiplier`, `autoFlowCalibration`, `profileFlowCalibration`, `setProfileFlowCalibration`, `clearProfileFlowCalibration`, `effectiveFlowCalibration`, `hasProfileFlowCalibration`, `allProfileFlowCalibrations`, `perProfileFlowCalVersion`, `flowCalPendingIdeals`, `appendFlowCalPendingIdeal`, `clearFlowCalPendingIdeals`, plus the `flowCalibrationMultiplierChanged`, `autoFlowCalibrationChanged`, and `perProfileFlowCalibrationChanged` signals.

The SAW learning surface comprises: `sawLearnedLag`, `sawLearnedLagFor`, `getExpectedDrip`, `getExpectedDripFor`, `sawLearningEntries`, `sawLearningEntriesFor`, `sawModelSource`, `addSawLearningPoint`, `resetSawLearning`, `resetSawLearningForProfile`, `isSawConverged`, `perProfileSawHistory`, `allPerProfileSawHistory`, `sawPendingBatch`, `globalSawBootstrapLag`, `setGlobalSawBootstrapLag`, the static `sensorLag(scaleType)` helper, plus the `sawLearnedLagChanged` signal and the file-scope constants `kSawMinMediansForGraduation`, `kBatchSize`, `kMaxPairHistory`, `kBatchMaxIqr`, `kBatchMaxDeviation`.

#### Scenario: Calibration surface lives on the sub-object
- **WHEN** a developer searches `src/core/settings.h` for `flowCalibrationMultiplier`, `sawLearnedLag`, `profileFlowCalibration`, `addSawLearningPoint`, `getExpectedDrip`, `sawModelSource`, or any other listed name
- **THEN** zero matches are found in `settings.h`
- **AND** every listed name appears only in `settings_calibration.h` and `settings_calibration.cpp`

#### Scenario: Calibration cross-domain reset goes through connect-based wiring
- **WHEN** `SettingsCalibration::resetSawLearning` is invoked from QML or C++
- **THEN** the sub-object emits a `sawLearningResetRequested` signal (or equivalent) and does not directly call `SettingsBrew` setters
- **AND** the hot-water SAW offset and sample count are reset on `SettingsBrew` via a `connect()` established in the `Settings::Settings()` constructor body
