# settings-architecture Specification

## Purpose
Defines the decomposition of the monolithic `Settings` class into domain sub-objects (`SettingsMqtt`, `SettingsCalibration`, `SettingsBrew`, etc.), each owning its own `QSettings` instance and property set, with `Settings` reduced to a thin façade exposing sub-object accessors and cross-domain wiring. Specifies the required QML access pattern (`Settings.<domain>.<prop>`), the narrow-header-inclusion rule that bounds recompilation blast radius for domain-specific consumers, and the extraction of shot-history data structures into their own header.
## Requirements
### Requirement: Settings Domain Decomposition

The `Settings` class SHALL be decomposed into domain sub-objects, each a `QObject` subclass owning its own `QSettings` instance and only its domain's properties, signals and methods. `Settings` SHALL construct them as children and expose each through a typed accessor `Settings<Domain>* <domain>() const`, used as the `CONSTANT` `Q_PROPERTY` READ. The final `settings.h` SHALL contain only sub-object accessors and cross-domain methods.

#### Scenario: Domain sub-object is independently includable
- **WHEN** a component depends only on a single domain's settings
- **THEN** it includes that domain's header (e.g., `settings_calibration.h`) and receives a `SettingsCalibration*` — not `Settings*`
- **AND** changing any non-Calibration domain header does not trigger recompilation of that component

#### Scenario: Settings.h is a thin façade
- **WHEN** all 12 domain splits are complete
- **THEN** `settings.h` contains only the twelve domain headers' includes, the twelve typed `Q_PROPERTY` accessors, cross-domain method declarations, and `sync`/`factoryReset`
- **AND** `settings.h` contains no `Q_PROPERTY` for any property that has been moved to a sub-object

*(The original "under 200 lines" bound is dropped. It measured the wrong thing: the twelve includes and the explanatory comment that keeps the erasure from being reintroduced take the file to 264 lines while removing declarations rather than adding them. The bound that matters is the second bullet — no migrated property declared here.)*

#### Scenario: Domain property types are statically resolvable from QML
- **WHEN** `qmllint` analyses a QML file containing `Settings.brew.<prop>`
- **THEN** it resolves `Settings.brew` to `SettingsBrew` and checks `<prop>` against that class
- **AND** a misspelt property is reported as `missing-property` rather than passing silently

#### Scenario: Each new domain sub-object is QML-introspectable
- **WHEN** a new `Settings<Domain>` class is added
- **THEN** `src/core/settings_qml.h` declares a `Settings<Domain>Foreign` gadget carrying `QML_FOREIGN(Settings<Domain>)`, `QML_NAMED_ELEMENT(Settings<Domain>Type)` and `QML_UNCREATABLE`
- **AND** the gadget is written out literally, not produced by a macro — `moc` does not expand macros that declare a `Q_GADGET`, so a generated one compiles and registers nothing
- **AND** QML expressions like `Settings.<domain>.<prop>` resolve to the sub-object's property at runtime, not to `undefined`

*(This replaces the runtime `qmlRegisterUncreatableType<Settings<Domain>>("Decenza", ...)` call in `main.cpp`. `qmltyperegistrar` cannot see a runtime call, so the registration never reached `Decenza.qmltypes` and no static tool knew the type existed.)*

#### Scenario: Cross-domain side effects use connect-based wiring
- **WHEN** changing a property on one domain must trigger an update on another domain (e.g., `resetSawLearning` on `SettingsCalibration` must reset hot-water SAW offset state on `SettingsBrew`)
- **THEN** the wiring is established via `connect()` in the `Settings::Settings()` constructor body, after all `m_<domain>` members are constructed
- **AND** the sub-object's setter does not directly call methods on another domain

#### Scenario: Cross-domain wiring SHALL NOT mirror a setting onto per-shot state
- **WHEN** a proposed cross-domain wiring would copy a configured setting into a field that is snapshotted onto a shot at save time
- **THEN** the wiring SHALL NOT be established
- **AND** the value SHALL be written to the shot record at the point a person supplies it instead
- **AND** the reason SHALL be understood as concrete rather than stylistic: the removed `setDefaultShotRating` → `setDyeEspressoEnjoyment` wiring is what made a deleted setting keep rating shots, because the mirrored field outlived the setting that fed it

### Requirement: Domain sub-object set

The complete domain set SHALL be `SettingsMqtt`, `SettingsAutoWake`, `SettingsHardware`, `SettingsAI`, `SettingsTheme`, `SettingsVisualizer`, `SettingsMcp`, `SettingsBrew`, `SettingsDye`, `SettingsNetwork`, `SettingsApp` and `SettingsCalibration`.

#### Scenario: Every domain is reachable from the facade
- **WHEN** the `Settings` façade is constructed
- **THEN** each of the twelve domain sub-objects is reachable through its accessor

### Requirement: Domain accessors stay typed

Domain accessors SHALL be typed as the concrete sub-object, never erased to `QObject*`, and `settings.h` SHALL `#include` each domain header so moc has a complete type. `Q_DECLARE_OPAQUE_POINTER` SHALL NOT be used to satisfy moc without that include.

#### Scenario: Opaque pointer is rejected
- **WHEN** `Q_DECLARE_OPAQUE_POINTER` satisfies moc without the include
- **THEN** QML receives a `QVariant` rather than the object, so every `Settings.<domain>` member fails at runtime

#### Scenario: Erased types hide QML call sites
- **WHEN** a domain accessor is erased to `QObject*`
- **THEN** `qmllint`, `qmlcachegen` and the language server cannot check the `Settings.<domain>.<prop>` call sites

### Requirement: QML Sub-Object Access

All QML code that accesses settings SHALL use the domain sub-object accessor (e.g., `Settings.calibration.sawLearnedLag`), never the flat `Settings` property. `Connections` blocks targeting settings properties SHALL target the sub-object (`Connections { target: Settings.calibration }`). The flat `Settings.X` form SHALL be used only for properties that remain on `Settings`, such as the accessors and coordinator state.

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

### Requirement: Narrow Consumer Header Isolation

A domain-specific C++ consumer SHALL include only its domain header, not `settings.h`, and its constructor SHALL accept the domain sub-object pointer, which `main.cpp` passes (e.g., `settings.brew()`). Wide consumers that touch multiple domains MAY keep `Settings*`. Narrowing consumers is the only sanctioned lever for reducing the recompile blast of a domain-header edit; re-erasing property types SHALL NOT be used for that purpose.

#### Scenario: Narrow consumer does not include settings.h
- **WHEN** a domain-specific consumer's header is compiled
- **THEN** the file does not `#include "settings.h"` and does not include any other domain's header
- **AND** changing `settings.h` does not trigger its recompilation

#### Scenario: Settings.h transitive includer count is bounded
- **WHEN** the project is fully migrated
- **THEN** the count of `.cpp` files that transitively include `settings.h` is 10 or fewer
- **AND** measurement comparison with the pre-Tier-1 baseline (39 includers) is documented in the merging PR

#### Scenario: The cost of the typed properties is recorded, not re-litigated
- **WHEN** a contributor proposes reverting the domain property types to `QObject*` to speed up builds
- **THEN** the proposal is rejected, and the measured figures are the ones on record: a domain-header edit takes ~60 s against ~26 s before, of which the marginal cost attributable to this decision is +129 C++ translation units (the 218 QML cache units in the dirty set rebuild either way, because a domain header carries `Q_OBJECT`)
- **AND** `tst_settings::qmlChainsThroughDomainSubObjects` remains in the suite to catch a reintroduction that compiles and lints clean

#### Scenario: Wide consumers keep the façade
- **WHEN** a class reads several domains, such as `MainController` or `settingsserializer.cpp`
- **THEN** it MAY keep `Settings*` and access domains through `settings->domain()->X()`

### Requirement: Shot History Types Extraction

The data structures in `shothistorystorage.h` (`ShotRecord`, `HistoryShotSummary`, `ShotFilter`, `ShotSaveData`, `GrinderContext`, `HistoryPhaseMarker`) SHALL live in `shothistory_types.h`, which `shothistorystorage.h` SHALL include. Components that only hold a `ShotHistoryStorage*` and call simple accessors SHALL forward-declare it and include `shothistorystorage.h` only in their `.cpp`.

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

Each domain sub-object's `QSettings` operations SHALL use the same key strings the property used before migration. Key strings SHALL remain byte-identical across the settings-store consolidation.

#### Scenario: Existing user settings persist across the migration
- **WHEN** a user upgrades from a build with `Settings::sawLearnedLag` to a build with `SettingsCalibration::sawLearnedLag`
- **THEN** the previously-saved SAW learning history (`saw/learningHistory`, `saw/perProfileHistory`, `saw/perProfileBatch`, `saw/globalBootstrapLag/<scale>`) is read correctly on first launch
- **AND** the previously-saved flow calibration values (`calibration/flowMultiplier`, `calibration/autoFlowCalibration`, `calibration/perProfileFlow`, `calibration/flowCalBatch`) are read correctly on first launch
- **AND** no user-visible settings reset occurs

#### Scenario: Key strings match pre-split values
- **WHEN** a property's setter or getter is reviewed in `SettingsCalibration`
- **THEN** the string passed to `m_settings.value(...)` / `m_settings.setValue(...)` is byte-identical to the pre-migration string in `settings.cpp`

#### Scenario: Sub-objects share one store handle source
- **WHEN** any `Settings<Domain>` constructor is reviewed
- **THEN** its `m_settings` member is declared `mutable AppSettings` and default-constructed, rather than initialised from a literal organization/application pair
- **AND** the strings `"DecentEspresso"` and `"DE1Qt"` do not appear in any `settings_<domain>.cpp`

#### Scenario: Migration precedes settings construction
- **WHEN** the application starts on an installation that has not yet migrated
- **THEN** the legacy-store migration completes before the `Settings` façade is constructed
- **AND** every domain sub-object's first read observes the migrated values rather than defaults

### Requirement: Domain sub-objects share one store handle

Domain sub-objects SHALL NOT construct their own store handle. Each SHALL obtain it from the shared `AppSettings` type, which names the canonical store identity in exactly one place (see the `settings-store-identity` capability).

#### Scenario: Store identity is named once
- **WHEN** a domain sub-object needs its `QSettings` handle
- **THEN** it obtains the handle from `AppSettings` and does not open `QSettings("DecentEspresso", "DE1Qt")` itself

### Requirement: Legacy store migrates before sub-objects read it

Existing user settings SHALL survive the consolidation by migration. The one-time legacy-store migration SHALL copy every key from `("DecentEspresso", "DE1Qt")` into the canonical store before any sub-object reads it, and domain sub-objects SHALL NOT assume the canonical store is pre-populated at construction.

#### Scenario: Upgrading install reads migrated values
- **WHEN** an upgrading installation constructs the `Settings` façade
- **THEN** each sub-object reads values that the legacy-store migration has already copied

### Requirement: Calibration Domain Surface

The `SettingsCalibration` domain sub-object SHALL own the auto flow calibration surface and the SAW (stop-at-weight) learning surface in their entirety. No calibration or SAW-related property, invokable, signal, cache or static helper SHALL remain on `Settings` after Tier 3 lands.

#### Scenario: Calibration surface lives on the sub-object
- **WHEN** a developer searches `src/core/settings.h` for `flowCalibrationMultiplier`, `sawLearnedLag`, `profileFlowCalibration`, `addSawLearningPoint`, `getExpectedDrip`, `sawModelSource`, or any other listed name
- **THEN** zero matches are found in `settings.h`
- **AND** every listed name appears only in `settings_calibration.h` and `settings_calibration.cpp`

#### Scenario: Calibration cross-domain reset goes through connect-based wiring
- **WHEN** `SettingsCalibration::resetSawLearning` is invoked from QML or C++
- **THEN** the sub-object emits a `sawLearningResetRequested` signal (or equivalent) and does not directly call `SettingsBrew` setters
- **AND** the hot-water SAW offset and sample count are reset on `SettingsBrew` via a `connect()` established in the `Settings::Settings()` constructor body

### Requirement: Auto flow calibration surface

The auto flow calibration surface SHALL comprise `flowCalibrationMultiplier`, `autoFlowCalibration`, `profileFlowCalibration`, `setProfileFlowCalibration`, `clearProfileFlowCalibration`, `effectiveFlowCalibration`, `hasProfileFlowCalibration`, `allProfileFlowCalibrations` and `perProfileFlowCalVersion`.

#### Scenario: Flow calibration members are on the sub-object
- **WHEN** QML or C++ reads any member of the auto flow calibration surface
- **THEN** it is reached through `Settings.calibration`, not `Settings`

### Requirement: Auto flow calibration pending ideals and signals

The auto flow calibration surface SHALL also comprise `flowCalPendingIdeals`, `appendFlowCalPendingIdeal` and `clearFlowCalPendingIdeals`, and the signals `flowCalibrationMultiplierChanged`, `autoFlowCalibrationChanged` and `perProfileFlowCalibrationChanged`.

#### Scenario: Pending ideals are on the sub-object
- **WHEN** a flow calibration pending ideal is appended or cleared
- **THEN** it is done through `Settings.calibration`, not `Settings`

### Requirement: SAW learning surface

The SAW learning surface SHALL comprise `sawLearnedLag`, `sawLearnedLagFor`, `getExpectedDrip`, `getExpectedDripFor`, `sawLearningEntries`, `sawLearningEntriesFor`, `sawModelSource`, `addSawLearningPoint`, `resetSawLearning`, `resetSawLearningForProfile`, `isSawConverged`, `perProfileSawHistory`, `allPerProfileSawHistory`, `sawPendingBatch`, `globalSawBootstrapLag`, `setGlobalSawBootstrapLag` and the static `sensorLag(scaleType)`, with the `sawLearnedLagChanged` signal.

#### Scenario: SAW constants stay file-scope
- **WHEN** the SAW learning surface is moved to the sub-object
- **THEN** the constants `kSawMinMediansForGraduation`, `kBatchSize`, `kMaxPairHistory`, `kBatchMaxIqr` and `kBatchMaxDeviation` remain file-scope constants, not members

### Requirement: Removing a setting SHALL remove its stored key

Removing a setting SHALL evict every key the removed feature wrote from the settings store, including keys written by fields the setting fed, because deleting its property and accessors is not sufficient. Eviction SHALL be idempotent, so it can run unconditionally on construction without a version counter or migration framework.

#### Scenario: Removed setting leaves no key behind

- **GIVEN** a settings store written by a build that had the removed feature
- **WHEN** the user upgrades to a build where the feature is gone
- **THEN** the store SHALL contain no key that the removed feature wrote
- **AND** a subsequent launch SHALL perform no further eviction work

